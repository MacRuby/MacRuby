require File.expand_path('../builder/builder', __FILE__)

desc "Build the markgc tool"
task :mark_gc do
  if !File.exist?('markgc')
    sh "/usr/bin/gcc -std=c99 markgc.c -o markgc -Wno-format"
  end
end

desc "Build the plblockimp library for imp_implementationWithBlock() support."
task :plblockimp do
  # Prepare assembly trampolines for plblockimp
  plblockimp_sources = []
  plblockimp_targets = []
  ARCHS.each do |a|
    tramp_sources = ["", "_stret"].map { |s|
        "plblockimp/#{a}/blockimp_#{a}#{s}.tramp"
    }
    tramp_sources.each do |s|
      unless sh "plblockimp/gentramp.sh #{s} #{a} plblockimp/"
        $stderr.puts "Failed to generate trampolines for plblockimp"
        exit 1
      end
    end
    plblockimp_sources.concat( ["", "_stret"].map { |s|
        "plblockimp/blockimp_#{a}#{s}_config.c"
    } )
    as_sources = ["", "_stret"].map { |s|
        "plblockimp/blockimp_#{a}#{s}.s"
    }
    as_sources.each do |s|
      t = s.sub(%r{\.s$}, ".o")
      if !File.exist?(t) || File.mtime(t) < File.mtime(s)
        unless sh "as -arch #{a} -o #{t} #{s}"
          $stderr.puts "Failed to assemble trampolines for plblockimp"
          exit 1
        end
      end
      plblockimp_targets << t
    end
  end

  # Build plblockimp as an object file for later linking
  plblockimp_sources.concat( ["blockimp.c", "trampoline_table.c"].map { |s|
      "plblockimp/#{s}"
  } )
  cflags = $builder.cflags.scan(%r{-[^D][^\s]*}).join(' ').sub(%r{-arch},'')
  plblockimp_sources.each do |s|
    t = s.sub(%r{.c$}, ".o")
    a = ARCHS.map { |x| "-arch #{x}" }.join(' ')
    if !File.exist?(t) || File.mtime(t) < File.mtime(s)
      unless sh "#{CC} #{a} -c #{cflags} -DPL_BLOCKIMP_PRIVATE -o #{t} #{s}"
        exit 1
      end
    end
    plblockimp_targets << t
  end

  need_compile = false
  obj = "#{$builder.objsdir}/plblockimp.o"
  plblockimp_targets.each do |t|
    if !File.exist?(obj) || File.mtime(obj) < File.mtime(t)
      need_compile = true
      break
    end
  end

  if need_compile
    plbi_o = plblockimp_targets.join(' ')
    unless sh "ld #{plbi_o} -r -o #{$builder.objsdir}/plblockimp.o"
      $stderr.puts "Failed to link plblockimp components"
      exit 1
    end
  end
end

task :files => [:config_h, :dtrace_h, :revision_h, :mark_gc, :plblockimp] do
end

def build_objects
  if !File.exist?('parse.c') or File.mtime('parse.y') > File.mtime('parse.c')
    sh("/usr/bin/bison -o y.tab.c parse.y")
    sh("/usr/bin/sed -f ./tool/ytab.sed -e \"/^#/s!y\.tab\.c!parse.c!\" y.tab.c > parse.c.new")
    if !File.exist?('parse.c') or File.read('parse.c.new') != File.read('parse.c')
      mv('parse.c.new', 'parse.c')
      rm_f(File.join($builder.objsdir, 'parse.o'))
    else
      rm('parse.c.new')
    end
  end
  if !File.exist?('lex.c') or File.read('lex.c') != File.read('lex.c.blt')
    cp('lex.c.blt', 'lex.c')
  end
  if !File.exist?('node_name.inc') or File.mtime('include/ruby/node.h') > File.mtime('node_name.inc')
    sh("/usr/bin/ruby -n tool/node_name.rb include/ruby/node.h > node_name.inc")
  end
  kernel_data_c = File.join($builder.objsdir, 'kernel_data.c')
  if !File.exist?(kernel_data_c) or File.mtime('kernel.c') > File.mtime(kernel_data_c)
    # Locate llvm-gcc...
    path = ENV['PATH'].split(':')
    path.unshift('/Developer/usr/bin')
    llvm_gcc = path.map { |x| File.join(x, 'llvm-gcc') }.find { |x| File.exist?(x) }
    unless llvm_gcc
      $stderr.puts "Cannot locate llvm-gcc in given path: #{path}"
      exit 1
    end
    opt = File.join(LLVM_PATH, 'bin/opt')
    unless File.exist?(opt)
      $stderr.puts "Cannot locate opt in given LLVM path: #{LLVM_PATH}"
    end
    sh "echo '' > #{kernel_data_c}"
    cflags = $builder.cflags.scan(/-I[^\s]+/).join(' ')
    cflags << ' ' << $builder.cflags.scan(/-D[^\s]+/).join(' ')
    $builder.archs.each do |x| 
      output = File.join($builder.objsdir, "kernel-#{x}.bc")
      # Compile the IR for the kernel.c source file & optimize it.
      sh "#{llvm_gcc} -arch #{x} -fexceptions -fno-stack-protector -fwrapv #{cflags} --emit-llvm -c kernel.c -o #{output}"
      sh "#{opt} -O3 #{output} -o=#{output}"
      # Convert the bitcode into a C static array. We append a null byte to the
      # bitcode file because xxd doesn't, and it's needed by the bitcode
      # reader later at runtime.
      cp output, "#{output}.old"
      sh "/bin/dd if=/dev/zero count=1 bs=1 conv=notrunc >> #{output} 2>/dev/null"
      sh "/usr/bin/xxd -i #{output} >> #{kernel_data_c}"
      mv "#{output}.old", output
    end
  end



  dispatcher_o = File.join($builder.objsdir, 'dispatcher.o')
  t = File.exist?(dispatcher_o) ? File.mtime(dispatcher_o) : nil
  vm_o = File.join($builder.objsdir, 'vm.o')
  t_vm = File.exist?(vm_o) ? File.mtime(vm_o) : nil
  $builder.build
  if t == nil or File.mtime(dispatcher_o) > t or t_vm == nil or File.mtime(vm_o) > t_vm
    # dispatcher.o must be marked as GC compliant to avoid a linker problem.
    # We do not build it using -fobjc-gc because gcc generates unnecessary (and slow)
    # write barriers.
    sh "./markgc #{dispatcher_o}"
  end
end

desc "Create miniruby"
task :miniruby => :files do
  $builder.config = FULL_CONFIG
  build_objects
  $builder.link_executable('miniruby', OBJS + ['plblockimp'])
end

desc "Create config file"
task :rbconfig => :miniruby do
  require File.expand_path('../builder/templates', __FILE__)
  Builder.create_rbconfig
end

namespace :macruby do
  desc "Build dynamic library"
  task :dylib => [:rbconfig, :files] do
    $builder.config = FULL_CONFIG
    build_objects
    dylib = "lib#{RUBY_SO_NAME}.#{NEW_RUBY_VERSION}.dylib"
    $builder.link_dylib(dylib, $builder.objs - ['main', 'gc-stub'] + ['plblockimp'])
    major, minor, teeny = NEW_RUBY_VERSION.scan(/\d+/)
    ["lib#{RUBY_SO_NAME}.#{major}.#{minor}.dylib", "lib#{RUBY_SO_NAME}.dylib"].each do |dylib_alias|
      if !File.exist?(dylib_alias) or File.readlink(dylib_alias) != dylib
        rm_f(dylib_alias)
        ln_s(dylib, dylib_alias)
      end
    end
  end

  desc "Build MacRuby"
  task :build => [:dylib] do
    $builder.config = FULL_CONFIG
    $builder.link_executable(RUBY_INSTALL_NAME, ['main', 'gc-stub'], "-L. -l#{RUBY_SO_NAME} -lobjc")
  end
end

DESTDIR = (ENV['DESTDIR'] or "")
EXTOUT = (ENV['EXTOUT'] or ".ext")
INSTALLED_LIST = '.installed.list'

desc "Build extensions"
task :extensions => [:miniruby] do
  Builder::Ext.build
end

desc "Generate RDoc files"
task :doc => [:macruby, :extensions] do
  doc_op = './doc'
  unless File.exist?(doc_op)
    sh "DYLD_LIBRARY_PATH=. ./macruby -I. -I./lib -I./ext/libyaml -I./ext/etc bin/rdoc --ri --op \"#{doc_op}\""
  end
end

AOT_STDLIB = [
  'rbconfig.rb',
  'lib/date.rb',
  'lib/date/**/*.rb',
  'lib/erb.rb',
  'lib/fileutils.rb',
  'lib/irb.rb',
  'lib/irb/**/*.rb',
  'lib/net/**/*.rb',
  'lib/optparse.rb',
  #'lib/stringio.rb', #spec fails
  'lib/rake.rb',
  'lib/rake/**/*.rb',
  'lib/rexml.rb',
  'lib/rexml/**/*.rb',
  'lib/{r,}ubygems.rb',
  'lib/rubygems/**/*.rb',
  'lib/thread.rb',
  'lib/time.rb',
  'lib/timeout.rb',
  'lib/uri.rb',
  'lib/uri/**/*.rb',
  'lib/xmlrpc/**/*.rb',
  'lib/yaml.rb',
  'lib/yaml/rubytypes.rb',
  'ext/**/lib/**/*.rb'
]
namespace :stdlib do
  desc "AOT compile the stdlib"
  task :build => [:miniruby, 'macruby:dylib'] do
    archf = ARCHS.map { |x| "--arch #{x}" }.join(' ')
    commands = (COMPILE_STDLIB ? AOT_STDLIB : %w{ rbconfig.rb }).map do |pattern|
      Dir.glob(pattern).map do |path|
        out = File.join(File.dirname(path), File.basename(path, '.rb') + '.rbo')
        if !File.exist?(out) or File.mtime(path) > File.mtime(out) or File.mtime('./miniruby') > File.mtime(out)
          "VM_OPT_LEVEL=0 ./miniruby -I. -I./lib bin/rubyc --internal #{archf} -C \"#{path}\" -o \"#{out}\""
        end
      end
    end.flatten.compact
    Builder.parallel_execute(commands)
  end
end

desc "Same as extensions"
task :ext => 'extensions'

desc "Create the plist file for the framework"
task :info_plist do
  require File.expand_path('../builder/templates', __FILE__)
  Builder.create_framework_info_plist
end

namespace :clean do
  desc "Clean local build files"
  task :local do
    CONFIGS.each { |x| rm_rf(x.objsdir) }
    list = ['parse.c', 'lex.c', INSTALLED_LIST, 'Makefile', RUBY_INSTALL_NAME, 'miniruby', 'kernel_data.c']
    list.concat(Dir['*.inc'])
    list.concat(Dir['lib*.{dylib,a}'])
    list.concat(Dir['plblockimp/*.o'])
    ARCHS.each do |a|
      plbi_s_t = "plblockimp/blockimp_#{a}"
      ['', '_stret'].each do |x|
        ['.h', '_config.c', '.s'].each do |y|
          list << plbi_s_t + x + y
        end
      end
    end
    list.each { |x| rm_f(x) }
  end

  desc "Clean .rbo build files"
  task :rbo do
    list = []
    list.concat(Dir['*.rbo'])
    list.concat(Dir['lib/**/*.rbo'])
    list.concat(Dir['ext/**/*.rbo'])
    list.each { |x| rm_f(x) }
  end

  desc "Clean extension build files"
  task :ext do
    Builder::Ext.clean
  end

  desc "Clean the RDoc files"
  task :doc do
    rm_rf('doc')
  end

  desc "Clean the Info.plist file"
  task :info_plist do
    rm_f('framework/Info.plist')
  end
end
