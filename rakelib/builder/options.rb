# User customizable variables.
# These variables can be set from the command line. Example:
#    $ rake framework_instdir=~/Library/Frameworks sym_instdir=~/bin

class Builder
  def self.options
    @options ||= {}
  end

  def self.option(name, default)
    options[name] = default

    if val = ENV[name]
      block_given? ? yield(val) : val
    else
      default
    end
  end
end

b = Builder

ARCHS =
  if s = ENV['RC_ARCHS']
    $stderr.puts "Getting archs from RC_ARCHS!"
    s.strip.split(/\s+/)
  else
    b.option('archs', `arch`.include?('ppc') ? 'ppc' : %w{x86_64}) { |x| x.split(',') }
  end

llvm_default_path = '/usr/local'
if `sw_vers -productVersion`.strip.to_f >= 10.7 and File.exist?('/AppleInternal')
  $stderr.puts "Welcome bleeding-edge adventurer!"
  llvm_default_path = '/Developer/usr/local'
  ENV['LLVM_TOT'] = '1'
end

RUBY_INSTALL_NAME = b.option('ruby_install_name', 'macruby')
RUBY_SO_NAME = b.option('ruby_so_name', RUBY_INSTALL_NAME)
LLVM_PATH = b.option('llvm_path', llvm_default_path)
FRAMEWORK_NAME = b.option('framework_name', 'MacRuby')
FRAMEWORK_INSTDIR = b.option('framework_instdir', '/Library/Frameworks')
SYM_INSTDIR = b.option('sym_instdir', '/usr/local')
ENABLE_DEBUG_LOGGING = b.option('enable_debug_logging', true) { |x| x == 'true' }
SIMULTANEOUS_JOBS = b.option('jobs', 1) { |x| x.to_i }
COMPILE_STDLIB = b.option('compile_stdlib', true) { |x| x == 'true' }
OPTZ_LEVEL = b.option('optz_level', 3) { |x| x.to_i }
IPHONEOS_SDK = b.option('iphoneos_sdk', nil)

default_CC = '/usr/bin/gcc-4.2'
unless File.exist?(default_CC)
  default_CC = '/usr/bin/gcc'
end
CC = b.option('CC', default_CC)

default_CXX = '/usr/bin/g++-4.2'
unless File.exist?(default_CXX)
  default_CXX = '/usr/bin/g++'
end
CXX = b.option('CXX', default_CXX)

EXTRA_CFLAGS = b.option('CFLAGS', '')

# Everything below this comment should *not* be modified.

if ENV['build_as_embeddable']
  $stderr.puts "The 'build_as_embeddable' build configuration has been removed because it is no longer necessary. To package a full version of MacRuby inside your application, please use the `Embed MacRuby' target for Xcode apps or `macruby_deploy` in a script."
  exit 1
end

#verbose(true)

if `sw_vers -productVersion`.strip < '10.5.6'
  $stderr.puts "Sorry, your environment is not supported. MacRuby requires Mac OS X 10.5.6 or higher."
  exit 1
end

if `arch`.include?('ppc')
  $stderr.puts "You appear to be using a PowerPC machine. MacRuby's primary architectures are Intel 32-bit and 64-bit (i386 and x86_64). Consequently, PowerPC support may be lacking some features."
end

LLVM_CONFIG = File.join(LLVM_PATH, 'bin/llvm-config')
unless File.exist?(LLVM_CONFIG)
  $stderr.puts "The llvm-config executable was not located at #{LLVM_CONFIG}. Please make sure LLVM is correctly installed on your machine and pass the llvm_path option to rake if necessary. Example: $ rake llvm_path=/path/"
  exit 1
end

if OPTZ_LEVEL < 0 || OPTZ_LEVEL > 4
  $stderr.puts "Incorrect optimization level: #{OPTZ_LEVEL}"
  exit 1
end

version_h = File.read('version.h')
NEW_RUBY_VERSION = version_h.scan(/#\s*define\s+RUBY_VERSION\s+\"([^"]+)\"/)[0][0]
NEW_RUBY_MAJOR_VERSION = NEW_RUBY_VERSION.to_i
NEW_RUBY_MINOR_VERSION = NEW_RUBY_VERSION.match(/\d+\.(\d+)\./)[1]
unless defined?(MACRUBY_VERSION)
  MACRUBY_VERSION = version_h.scan(/#\s*define\s+MACRUBY_VERSION\s+\"(.*)\"/)[0][0]
end
INSTALL_VERSION = b.option('install_version', MACRUBY_VERSION)

uname_release_number = (ENV['UNAME_RELEASE'] or `uname -r`.scan(/^(\d+)\.\d+\.(\d+)/)[0].join('.'))
NEW_RUBY_PLATFORM = 'universal-darwin' + uname_release_number

FRAMEWORK_PATH = File.join(FRAMEWORK_INSTDIR, FRAMEWORK_NAME + '.framework')
FRAMEWORK_VERSION = File.join(FRAMEWORK_PATH, 'Versions', INSTALL_VERSION)
FRAMEWORK_USR = File.join(FRAMEWORK_VERSION, 'usr')
FRAMEWORK_USR_BIN = File.join(FRAMEWORK_USR, 'bin')
FRAMEWORK_USR_LIB = File.join(FRAMEWORK_USR, 'lib')
FRAMEWORK_USR_SHARE = File.join(FRAMEWORK_USR, 'share')
FRAMEWORK_USR_LIB_RUBY = File.join(FRAMEWORK_USR_LIB, 'ruby')
FRAMEWORK_RESOURCES = File.join(FRAMEWORK_VERSION, 'Resources')

RUBY_LIB = File.join(FRAMEWORK_USR_LIB_RUBY, NEW_RUBY_VERSION)
RUBY_ARCHLIB = File.join(RUBY_LIB, NEW_RUBY_PLATFORM)
RUBY_SITE_LIB = File.join(FRAMEWORK_USR_LIB_RUBY, 'site_ruby')
RUBY_SITE_LIB2 = File.join(RUBY_SITE_LIB, NEW_RUBY_VERSION)
RUBY_SITE_ARCHLIB = File.join(RUBY_SITE_LIB2, NEW_RUBY_PLATFORM)
RUBY_VENDOR_LIB = File.join(FRAMEWORK_USR_LIB_RUBY, 'vendor_ruby')
RUBY_VENDOR_LIB2 = File.join(RUBY_VENDOR_LIB, NEW_RUBY_VERSION)
RUBY_VENDOR_ARCHLIB = File.join(RUBY_VENDOR_LIB2, NEW_RUBY_PLATFORM)

INSTALL_NAME  = File.join(FRAMEWORK_USR_LIB, 'lib' + RUBY_SO_NAME + '.dylib')
# NOTE This gets expanded here instead of in rbconfig.rb
DYLIB_ALIASES = "lib#{RUBY_SO_NAME}.#{NEW_RUBY_MAJOR_VERSION}.#{NEW_RUBY_MINOR_VERSION}.dylib lib#{RUBY_SO_NAME}.dylib"
LLVM_MODULES  = "core jit nativecodegen bitwriter bitreader ipo"
EXPORTED_SYMBOLS_LIST = "./exported_symbols_list"

# Full list of objects to build.
OBJS = %w{
  array bignum class compar complex enum enumerator error eval file load proc
  gc hash env inits io math numeric object pack parse prec dir process
  random range rational re ruby signal sprintf st string struct time strftime
  util variable version thread id objc bs ucnv encoding main dln dmyext marshal
  gcd vm_eval gc-stub bridgesupport compiler dispatcher vm symbol debugger
  interpreter MacRuby MacRubyDebuggerConnector NSArray NSDictionary NSString
  transcode sandbox
}

# Additional compilation flags for certain objects.
OBJS_CFLAGS = {
  'dispatcher' => '-x objective-c++', # compile as Objective-C++.
  'bs' => '-I/usr/include/libxml2'    # need to access libxml2
}

class BuilderConfig
  attr_reader :objs, :archs, :cflags, :cxxflags, :objc_cflags, :ldflags,
    :objsdir, :objs_cflags, :dldflags, :CC, :CXX

  def initialize(opt)
    @CC = (opt.delete(:CC) || CC)
    @CXX = (opt.delete(:CXX) || CXX)
    @objs = (opt.delete(:objs) || OBJS)
    @archs = (opt.delete(:archs) || ARCHS)
    sdk = opt.delete(:sdk)
    has_libauto = sdk ? File.exist?("#{sdk}/usr/lib/libauto.dylib") : true
    archflags = archs.map { |x| "-arch #{x}" }.join(' ')
    @cflags = "-std=c99 -I. -I./include -pipe -fno-common -fexceptions -fblocks -fwrapv -g -O#{OPTZ_LEVEL} -Wall -Wno-deprecated-declarations -Werror #{archflags} #{EXTRA_CFLAGS}"
    @cxxflags = "-I. -I./include -fblocks -g -Wall -Wno-deprecated-declarations -Werror #{archflags} #{EXTRA_CFLAGS}"
    @ldflags = '-lpthread -ldl -lxml2 -lobjc -licucore -framework Foundation'
    @ldflags << " -lauto" if has_libauto
    @cxxflags << ' ' << `#{LLVM_CONFIG} --cxxflags #{LLVM_MODULES}`.sub(/-DNDEBUG/, '').sub(/-fno-exceptions/, '').sub(/-Wcast-qual/, '').sub!(/-O\d/, "-O#{OPTZ_LEVEL}").strip.gsub(/\n/, '')
    @cxxflags << ' -DLLVM_TOT' if ENV['LLVM_TOT']
    @ldflags << ' ' << `#{LLVM_CONFIG} --ldflags --libs #{LLVM_MODULES}`.strip.gsub(/\n/, '')
    unless has_libauto
      @cflags << ' -DNO_LIBAUTO'
      @cxxflags << ' -DNO_LIBAUTO'
    end
    @cxxflags << " -fno-rtti" unless @cxxflags.index("-fno-rtti")
    @dldflags = "-dynamiclib -undefined suppress -flat_namespace -install_name #{INSTALL_NAME} -current_version #{MACRUBY_VERSION} -compatibility_version #{MACRUBY_VERSION} -exported_symbols_list #{EXPORTED_SYMBOLS_LIST}"
    @cflags << ' -I./icu-1060 -I./plblockimp'
    @cxxflags << ' -I./icu-1060 -I./plblockimp'
    if sdk
      sdk_flags = "--sysroot=#{sdk}"
      @cflags << " #{sdk_flags}"
      @cxxflags << " #{sdk_flags}"
    end
    @objc_cflags = cflags.dup
    @objc_cflags << ' -fobjc-gc-only' if has_libauto
    @objs_cflags = OBJS_CFLAGS
    @objsdir = opt.delete(:objsdir)
  end
end

FULL_CONFIG = BuilderConfig.new(:objsdir => '.objs')
CONFIGS = [FULL_CONFIG]

# We monkey-patch the method that Rake uses to display the tasks so we can add
# the build options.
require 'rake'
module Rake
  class Application
    def formatted_macruby_options
      Builder.options.sort_by { |name, _| name }.map do |name, default|
        default = default.join(',') if default.is_a?(Array)
        "        #{name.ljust(30)} \"#{default}\""
      end.join("\n")
    end

    alias_method :display_tasks_and_comments_without_macruby_options, :display_tasks_and_comments

    def display_tasks_and_comments
      display_tasks_and_comments_without_macruby_options
      puts %{
  To change any of the default build options, use the rake build task
  of choice with any of these following option-value pairs:

    Usage: $ rake [task] [option=value, ...]

      #{'Option:'.ljust(30)} Default value:

#{formatted_macruby_options}

    Example:

      $ rake all archs="i386,x86_64" framework_instdir="~/Library/Frameworks"

}
    end
  end
end
