# User customizable variables.
# These variables can be set from the command line. Example:
#    $ rake framework_instdir=~/Library/Frameworks sym_instdir=~/bin

$builder_options = {}

def do_option(name, default)
  $builder_options[name] = default
  
  val = ENV[name]
  if val
    if block_given?
      yield val
    else
      val
    end
  else
    default
  end
end

USE_CLANG = do_option('use_clang', false)
RUBY_INSTALL_NAME = do_option('ruby_install_name', 'macruby')
RUBY_SO_NAME = do_option('ruby_so_name', RUBY_INSTALL_NAME)
ARCHS = 
  if s = ENV['RC_ARCHS']
    $stderr.puts "getting archs from RC_ARCHS!"
    s.strip.split(/\s+/)
  else
    do_option('archs', `arch`.include?('ppc') ? 'ppc' : %w{i386 x86_64}) { |x| x.split(',') }
  end
FRAMEWORK_NAME = do_option('framework_name', 'MacRuby')
FRAMEWORK_INSTDIR = do_option('framework_instdir', '/Library/Frameworks')
SYM_INSTDIR = do_option('sym_instdir', '/usr/local')
NO_WARN_BUILD = !do_option('allow_build_warnings', false)
ENABLE_STATIC_LIBRARY = do_option('enable_static_library', 'no') { 'yes' }
ENABLE_DEBUG_LOGGING = do_option('enable_debug_logging', true) { |x| x == 'true' }

# Everything below this comment should *not* be modified.

if ENV['build_as_embeddable']
  $stderr.puts "The 'build_as_embeddable' build configuration has been removed because it is no longer necessary. To package a full version of MacRuby inside your application, please use `macrake deploy` for HotCocoa apps and the `Embed MacRuby` target for Xcode apps."
  exit 1
end

verbose(true)

if `sw_vers -productVersion`.strip < '10.5.6'
  $stderr.puts "Sorry, your environment is not supported. MacRuby requires Mac OS X 10.5.6 or higher." 
  exit 1
end

if `arch`.include?('ppc')
  $stderr.puts "You appear to be using a PowerPC machine. MacRuby's primary architectures are Intel 32-bit and 64-bit (i386 and x86_64). Consequently, PowerPC support may be lacking some features."
end

LLVM_CONFIG = `which llvm-config`.strip
if LLVM_CONFIG.empty?
  $stderr.puts "The `llvm-config' executable was not located in your PATH. Please make sure LLVM is correctly installed on your machine or that your PATH is correctly set."
  exit 1
end

version_h = File.read('version.h')
NEW_RUBY_VERSION = version_h.scan(/#\s*define\s+RUBY_VERSION\s+\"([^"]+)\"/)[0][0]
MACRUBY_VERSION = version_h.scan(/#\s*define\s+MACRUBY_VERSION\s+(.*)/)[0][0]

uname_release_number = (ENV['UNAME_RELEASE'] or `uname -r`.scan(/^(\d+)\.\d+\.(\d+)/)[0].join('.'))
NEW_RUBY_PLATFORM = 'universal-darwin' + uname_release_number

FRAMEWORK_PATH = File.join(FRAMEWORK_INSTDIR, FRAMEWORK_NAME + '.framework')
FRAMEWORK_VERSION = File.join(FRAMEWORK_PATH, 'Versions', MACRUBY_VERSION)
FRAMEWORK_USR = File.join(FRAMEWORK_VERSION, 'usr')
FRAMEWORK_USR_LIB = File.join(FRAMEWORK_USR, 'lib')
FRAMEWORK_USR_LIB_RUBY = File.join(FRAMEWORK_USR_LIB, 'ruby')

RUBY_LIB = File.join(FRAMEWORK_USR_LIB_RUBY, NEW_RUBY_VERSION)
RUBY_ARCHLIB = File.join(RUBY_LIB, NEW_RUBY_PLATFORM)
RUBY_SITE_LIB = File.join(FRAMEWORK_USR_LIB_RUBY, 'site_ruby')
RUBY_SITE_LIB2 = File.join(RUBY_SITE_LIB, NEW_RUBY_VERSION)
RUBY_SITE_ARCHLIB = File.join(RUBY_SITE_LIB2, NEW_RUBY_PLATFORM)
RUBY_VENDOR_LIB = File.join(FRAMEWORK_USR_LIB_RUBY, 'vendor_ruby')
RUBY_VENDOR_LIB2 = File.join(RUBY_VENDOR_LIB, NEW_RUBY_VERSION)
RUBY_VENDOR_ARCHLIB = File.join(RUBY_VENDOR_LIB2, NEW_RUBY_PLATFORM)

INSTALL_NAME = File.join(FRAMEWORK_USR_LIB, 'lib' + RUBY_SO_NAME + '.dylib')
ARCHFLAGS = ARCHS.map { |a| '-arch ' + a }.join(' ')
LLVM_MODULES = "core jit nativecodegen interpreter bitwriter"
if (USE_CLANG) and (`sw_vers -productVersion`.strip >= '10.6')
  CC = '/usr/bin/clang'
  CPP = '/usr/bin/llvm-g++-4.2'
elsif 
  CC = '/usr/bin/gcc'
  CPP = '/usr/bin/g++'
end
CFLAGS = "-I. -I./include -I./onig -I./giants/libDGiants  -I/usr/include/libxml2 #{ARCHFLAGS} -fno-common -pipe -O3 -g -Wall -fexceptions"
CFLAGS << " -Wno-parentheses -Wno-deprecated-declarations -Werror" if NO_WARN_BUILD
OBJC_CFLAGS = CFLAGS + " -fobjc-gc-only"
CXXFLAGS = `#{LLVM_CONFIG} --cxxflags #{LLVM_MODULES}`.strip
CXXFLAGS << " -I. -I./include -g -Wall #{ARCHFLAGS}"
CXXFLAGS << " -Wno-parentheses -Wno-deprecated-declarations -Werror" if NO_WARN_BUILD
LDFLAGS = `#{LLVM_CONFIG} --ldflags --libs #{LLVM_MODULES}`.strip.gsub(/\n/, '')
LDFLAGS << " -lpthread -ldl -lxml2 -lobjc -lauto -framework Foundation"
DLDFLAGS = "-dynamiclib -undefined suppress -flat_namespace -install_name #{INSTALL_NAME} -current_version #{MACRUBY_VERSION} -compatibility_version #{MACRUBY_VERSION}"

# removed: marshal
OBJS = %w{ 
  array bignum class compar complex enum enumerator error eval file load proc 
  gc hash inits io math numeric object pack parse prec dir process
  random range rational re onig/regcomp onig/regext onig/regposix onig/regenc
  onig/reggnu onig/regsyntax onig/regerror onig/regparse onig/regtrav
  onig/regexec onig/regposerr onig/regversion onig/enc/ascii onig/enc/unicode
  onig/enc/utf8 onig/enc/euc_jp onig/enc/sjis onig/enc/iso8859_1
  onig/enc/utf16_be onig/enc/utf16_le onig/enc/utf32_be onig/enc/utf32_le
  ruby set signal sprintf st string struct time transcode util variable version
  thread id objc bs encoding main dln dmyext
  vm_eval prelude miniprelude gc-stub bridgesupport compiler vm MacRuby
}

class Builder
  attr_reader :objs, :cflags, :cxxflags
  attr_accessor :objc_cflags, :ldflags, :dldflags

  def initialize(objs)
    @objs = objs.dup
    @cflags = CFLAGS
    @cxxflags = CXXFLAGS
    @objc_cflags = OBJC_CFLAGS
    @ldflags = LDFLAGS
    @dldflags = DLDFLAGS
    @obj_sources = {}
    @header_paths = {}
  end

  def build(objs=nil)
    objs ||= @objs
    objs.each do |obj| 
      if should_build?(obj) 
        s = obj_source(obj)
        cc, flags = 
          case File.extname(s)
            when '.c' then [CC, @cflags]
            when '.cpp' then [CPP, @cxxflags]
            when '.m' then [CC, @objc_cflags]
          end
        sh("#{cc} #{flags} -c #{s} -o #{obj}.o")
      end
    end
  end
 
  def link_executable(name, objs=nil, ldflags=nil)
    link(objs, ldflags, "-o #{name}", name)
  end

  def link_dylib(name, objs=nil, ldflags=nil)
    link(objs, ldflags, "#{@dldflags} -o #{name}", name)
  end

  def link_archive(name, objs=nil)
    objs ||= @objs
    if should_link?(name, objs)
      rm_f(name)
      sh("/usr/bin/ar rcu #{name} #{objs.map { |x| x + '.o' }.join(' ') }")
      sh("/usr/bin/ranlib #{name}")
    end
  end

  def clean
    @objs.map { |o| o + '.o' }.select { |o| File.exist?(o) }.each { |o| rm_f(o) }
  end
 
  private

  def link(objs, ldflags, args, name)
    objs ||= @objs
    ldflags ||= @ldflags
    if should_link?(name, objs)
      sh("#{CPP} #{@cflags} #{objs.map { |x| x + '.o' }.join(' ') } #{ldflags} #{args}")
    end
  end

  def should_build?(obj)
    if File.exist?(obj + '.o')
      src_time = File.mtime(obj_source(obj))
      obj_time = File.mtime(obj + '.o')
      src_time > obj_time \
        or dependencies[obj].any? { |f| File.mtime(f) > obj_time }
    else
      true
    end
  end

  def should_link?(bin, objs)
    if File.exist?(bin)
      mtime = File.mtime(bin)
      objs.any? { |o| File.mtime(o + '.o') > mtime }
    else
      true
    end
  end

  def err(*args)
    $stderr.puts args
    exit 1
  end

  def obj_source(obj)
    s = @obj_sources[obj]
    unless s
      s = ['.c', '.cpp', '.m'].map { |e| obj + e }.find { |p| File.exist?(p) }
      err "cannot locate source file for object `#{obj}'" if s.nil?
      @obj_sources[obj] = s
    end
    s
  end

  HEADER_DIRS = %w{. include include/ruby}
  def header_path(hdr)
    p = @header_paths[hdr]
    unless p
      p = HEADER_DIRS.map { |d| File.join(d, hdr) }.find { |p| File.exist?(p) }
      @header_paths[hdr] = p
    end
    p
  end
  
  def locate_headers(cont, src)
    txt = File.read(src)
    txt.scan(/#include\s+\"([^"]+)\"/).flatten.each do |header|
      p = header_path(header)
      if p and !cont.include?(p)
        cont << p
        locate_headers(cont, p)
      end
    end
  end
  
  def dependencies
    unless @obj_dependencies
      @obj_dependencies = {}
      @objs.each do |obj| 
        ary = []
        locate_headers(ary, obj_source(obj))
        @obj_dependencies[obj] = ary.uniq
      end
    end
    @obj_dependencies
  end
end

$builder = Builder.new(OBJS)
