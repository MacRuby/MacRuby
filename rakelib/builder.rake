require File.expand_path('../builder/builder', __FILE__)

desc "Build the markgc tool"
task :mark_gc do
  if !File.exist?('markgc')
    sh "/usr/bin/gcc -std=c99 markgc.c -o markgc -Wno-format"
  end
end

desc "Build known objects"
task :objects => [:config_h, :dtrace_h, :revision_h, :mark_gc] do
  if !File.exist?('parse.c') or File.mtime('parse.y') > File.mtime('parse.c')
    sh("/usr/bin/bison -o y.tab.c parse.y")
    sh("/usr/bin/sed -f ./tool/ytab.sed -e \"/^#/s!y\.tab\.c!parse.c!\" y.tab.c > parse.c.new")
    if !File.exist?('parse.c') or File.read('parse.c.new') != File.read('parse.c')
      mv('parse.c.new', 'parse.c')
      rm_f('parse.o')
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
  if !File.exist?('kernel_data.c') or File.mtime('kernel.c') > File.mtime('kernel_data.c')
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
    sh "echo '' > kernel_data.c"
    includes = CFLAGS.scan(/-I[^\s]+/).join(' ')
    ARCHS.each do |x| 
      output = "kernel-#{x}.bc"
      sh "#{llvm_gcc} -arch #{x} -fexceptions #{includes} --emit-llvm -c kernel.c -o #{output}"
      sh "#{opt} -O3 #{output} -o=#{output}"
      sh "/usr/bin/xxd -i #{output} >> kernel_data.c"
      sh "/bin/rm #{output}"
    end
  end
  t = File.exist?('dispatcher.o') ? File.mtime('dispatcher.o') : nil
  $builder.build
  if t == nil or File.mtime('dispatcher.o') > t
    # dispatcher.o must be marked as GC compliant to avoid a linker problem.
    # We do not build it using -fobjc-gc because gcc generates unnecessary (and slow)
    # write barriers.
    sh "./markgc ./dispatcher.o"
  end
end

desc "Create miniruby"
task :miniruby => :objects do
  $builder.link_executable('miniruby', OBJS)
end

desc "Create config file"
task :rbconfig => :miniruby do
  require File.expand_path('../builder/templates', __FILE__)
  Builder.create_rbconfig
end

namespace :macruby do
  desc "Build dynamic libraries for MacRuby"
  task :dylib => [:rbconfig, :miniruby] do
    dylib = "lib#{RUBY_SO_NAME}.#{NEW_RUBY_VERSION}.dylib"
    $builder.link_dylib(dylib, $builder.objs - ['main', 'gc-stub'])
    major, minor, teeny = NEW_RUBY_VERSION.scan(/\d+/)
    ["lib#{RUBY_SO_NAME}.#{major}.#{minor}.dylib", "lib#{RUBY_SO_NAME}.dylib"].each do |dylib_alias|
      if !File.exist?(dylib_alias) or File.readlink(dylib_alias) != dylib
        rm_f(dylib_alias)
        ln_s(dylib, dylib_alias)
      end
    end
  end

  desc "Build static libraries for MacRuby"
  task :static => :dylib do
    $builder.link_archive("lib#{RUBY_SO_NAME}-static.a", $builder.objs - ['main', 'gc-stub'])
  end

  desc "Build MacRuby"
  task :build => :dylib do
    $builder.link_executable(RUBY_INSTALL_NAME, ['main', 'gc-stub'], "-L. -l#{RUBY_SO_NAME} -lobjc")
  end
end

DESTDIR = (ENV['DESTDIR'] or "")
EXTOUT = (ENV['EXTOUT'] or ".ext")
INSTALLED_LIST = '.installed.list'
SCRIPT_ARGS = "--make=\"/usr/bin/make\" --dest-dir=\"#{DESTDIR}\" --extout=\"#{EXTOUT}\" --mflags=\"\" --make-flags=\"\""
INSTRUBY_ARGS = "#{SCRIPT_ARGS} --data-mode=0644 --prog-mode=0755 --installed-list #{INSTALLED_LIST} --mantype=\"doc\" --sym-dest-dir=\"#{SYM_INSTDIR}\" --rdoc-output=\"doc\""

desc "Build extensions"
task :extensions => [:miniruby, "macruby:static"] do
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
  'lib/rexml.rb',
  'lib/rexml/**/*.rb',
  'lib/rubygems.rb',
  'lib/rubygems/**/*.rb',
  'lib/thread.rb',
  'lib/time.rb',
  'lib/timeout.rb',
  'lib/uri.rb',
  'lib/uri/**/*.rb',
  'lib/xmlrpc/**/*.rb',
  'lib/yaml.rb',
  'lib/yaml/rubytypes.rb',
]
namespace :stdlib do
  desc "AOT compile the stdlib"
  task :build => [:miniruby, 'macruby:dylib'] do
    archf = ARCHS.map { |x| "--arch #{x}" }.join(' ')
    commands = (COMPILE_STDLIB ? AOT_STDLIB : %w{ rbconfig.rb }).map do |pattern|
      Dir.glob(pattern).map do |path|
        out = File.join(File.dirname(path), File.basename(path, '.rb') + '.rbo')
        if !File.exist?(out) or File.mtime(path) > File.mtime(out) or File.mtime('./miniruby') > File.mtime(out)
          "./miniruby -I. -I./lib bin/rubyc --internal #{archf} -C \"#{path}\" -o \"#{out}\""
        end
      end
    end.flatten.compact
    Builder.parallel_execute(commands)
  end
end

desc "Same as extensions"
task :ext => 'extensions'

namespace :framework do
  desc "Create the plist file for the framework"
  task :info_plist do
    require File.expand_path('../builder/templates', __FILE__)
    Builder.create_framework_info_plist
  end

  desc "Install the extensions"
  task :install_ext do
    Builder::Ext.install
  end

  desc "Install the framework"
  task :install => [:info_plist, :install_ext] do
    sh "./miniruby instruby.rb #{INSTRUBY_ARGS}"
  end
end

namespace :clean do
  desc "Clean local build files"
  task :local do
    $builder.clean
    list = ['parse.c', 'lex.c', INSTALLED_LIST, 'Makefile', RUBY_INSTALL_NAME, 'miniruby', 'kernel_data.c']
    list.concat(Dir['*.inc'])
    list.concat(Dir['lib*.{dylib,a}'])
    list.each { |x| rm_f(x) }
  end

  desc "Clean .rbo build files"
  task :rbo do
    list = []
    list.concat(Dir['*.rbo'])
    list.concat(Dir['lib/**/*.rbo'])
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
end
