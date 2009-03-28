require File.expand_path('../builder', __FILE__)
require 'rake'

# We monkey-patch the method that Rake uses to display the tasks so we can add
# the build options.
module Rake
  class Application
    def formatted_macruby_options
      $builder_options.sort_by { |name, _| name }.map do |name, default|
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

    Usage: $ rake [task] [option=value, …]

      #{'Option:'.ljust(30)} Default value:

#{formatted_macruby_options}

    Example:

      $ rake all archs="i386,ppc" framework_instdir="~/Library/Frameworks"

}
    end
  end
end

desc "Build known objects"
task :objects => [:config_h, :dtrace_h, :revision_h] do
  sh "/usr/bin/ruby tool/compile_prelude.rb prelude.rb miniprelude.c.new"
  if !File.exist?('miniprelude.c') or File.read('miniprelude.c') != File.read('miniprelude.c.new')
    mv('miniprelude.c.new', 'miniprelude.c')
  else
    rm('miniprelude.c.new')
  end
  if !File.exist?('prelude.c')
    touch('prelude.c') # create empty file nevertheless
  end
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
  $builder.build
end

desc "Create miniruby"
task :miniruby => :objects do
  $builder.link_executable('miniruby', OBJS - ['prelude'])
end

desc "Create config file"
task :rbconfig => :miniruby do
  rbconfig = <<EOS
# This file was created when MacRuby was built.  Any changes made to this file 
# will be lost the next time MacRuby is built.

module RbConfig
  RUBY_VERSION == "#{NEW_RUBY_VERSION}" or
    raise "ruby lib version (#{NEW_RUBY_VERSION}) doesn't match executable version (\#{RUBY_VERSION})"

  TOPDIR = File.dirname(__FILE__).chomp!("/lib/ruby/#{NEW_RUBY_VERSION}/#{NEW_RUBY_PLATFORM}")
  DESTDIR = '' unless defined? DESTDIR
  CONFIG = {}
  CONFIG["DESTDIR"] = DESTDIR
  CONFIG["INSTALL"] = '/usr/bin/install -c'
  CONFIG["prefix"] = (TOPDIR || DESTDIR + "#{FRAMEWORK_USR}")
  CONFIG["EXEEXT"] = ""
  CONFIG["ruby_install_name"] = "#{RUBY_INSTALL_NAME}"
  CONFIG["RUBY_INSTALL_NAME"] = "#{RUBY_INSTALL_NAME}"
  CONFIG["RUBY_SO_NAME"] = "#{RUBY_SO_NAME}"
  CONFIG["SHELL"] = "/bin/sh"
  CONFIG["PATH_SEPARATOR"] = ":"
  CONFIG["PACKAGE_NAME"] = ""
  CONFIG["PACKAGE_TARNAME"] = ""
  CONFIG["PACKAGE_VERSION"] = ""
  CONFIG["PACKAGE_STRING"] = ""
  CONFIG["PACKAGE_BUGREPORT"] = ""
  CONFIG["exec_prefix"] = "$(prefix)"
  CONFIG["bindir"] = "$(exec_prefix)/bin"
  CONFIG["sbindir"] = "$(exec_prefix)/sbin"
  CONFIG["libexecdir"] = "$(exec_prefix)/libexec"
  CONFIG["datarootdir"] = "$(prefix)/share"
  CONFIG["datadir"] = "$(datarootdir)"
  CONFIG["sysconfdir"] = "$(prefix)/etc"
  CONFIG["sharedstatedir"] = "$(prefix)/com"
  CONFIG["localstatedir"] = "$(prefix)/var"
  CONFIG["includedir"] = "$(prefix)/include"
  CONFIG["oldincludedir"] = "/usr/include"
  CONFIG["docdir"] = "$(datarootdir)/doc/$(PACKAGE)"
  CONFIG["infodir"] = "$(datarootdir)/info"
  CONFIG["htmldir"] = "$(docdir)"
  CONFIG["dvidir"] = "$(docdir)"
  CONFIG["pdfdir"] = "$(docdir)"
  CONFIG["psdir"] = "$(docdir)"
  CONFIG["libdir"] = "$(exec_prefix)/lib"
  CONFIG["localedir"] = "$(datarootdir)/locale"
  CONFIG["mandir"] = "$(datarootdir)/man"
  CONFIG["DEFS"] = ""
  CONFIG["ECHO_C"] = "\\\\\\\\c"
  CONFIG["ECHO_N"] = ""
  CONFIG["ECHO_T"] = ""
  CONFIG["LIBS"] = ""
  CONFIG["build_alias"] = ""
  CONFIG["host_alias"] = ""
  CONFIG["target_alias"] = ""
  CONFIG["BASERUBY"] = "ruby"
  CONFIG["MAJOR"], CONFIG["MINOR"], CONFIG["TEENY"] = [#{NEW_RUBY_VERSION.scan(/\d+/).map { |x| "\"" + x + "\"" }.join(', ')}]
  CONFIG["build"] = "i686-apple-darwin9.0.0"
  CONFIG["build_cpu"] = "i686"
  CONFIG["build_vendor"] = "apple"
  CONFIG["build_os"] = "darwin9.0.0"
  CONFIG["host"] = "i686-apple-darwin9.0.0"
  CONFIG["host_cpu"] = "i686"
  CONFIG["host_vendor"] = "apple"
  CONFIG["host_os"] = "darwin9.0.0"
  CONFIG["target"] = "i686-apple-darwin9.0.0"
  CONFIG["target_cpu"] = "i686"
  CONFIG["target_vendor"] = "apple"
  CONFIG["target_os"] = "darwin9.0"
  CONFIG["CC"] = "gcc"
  CONFIG["CFLAGS"] = "-fno-common -pipe $(cflags)"
  CONFIG["LDFLAGS"] = ""
  CONFIG["CPPFLAGS"] = "$(cppflags)"
  CONFIG["OBJEXT"] = "o"
  CONFIG["CXX"] = "g++"
  CONFIG["CXXFLAGS"] = ""
  CONFIG["CPP"] = "gcc -E"
  CONFIG["GREP"] = "/usr/bin/grep"
  CONFIG["EGREP"] = "/usr/bin/grep -E"
  CONFIG["GNU_LD"] = "no"
  CONFIG["CPPOUTFILE"] = "-o conftest.i"
  CONFIG["OUTFLAG"] = "-o "
  CONFIG["COUTFLAG"] = "-o "
  CONFIG["RANLIB"] = "ranlib"
  CONFIG["AR"] = "ar"
  CONFIG["AS"] = "as"
  CONFIG["ASFLAGS"] = ""
  CONFIG["NM"] = ""
  CONFIG["WINDRES"] = ""
  CONFIG["DLLWRAP"] = ""
  CONFIG["OBJDUMP"] = ""
  CONFIG["LN_S"] = "ln -s"
  CONFIG["SET_MAKE"] = ""
  CONFIG["INSTALL_PROGRAM"] = "$(INSTALL)"
  CONFIG["INSTALL_SCRIPT"] = "$(INSTALL)"
  CONFIG["INSTALL_DATA"] = "$(INSTALL) -m 644"
  CONFIG["RM"] = "rm -f"
  CONFIG["CP"] = "cp"
  CONFIG["MAKEDIRS"] = "mkdir -p"
  CONFIG["ALLOCA"] = ""
  CONFIG["DLDFLAGS"] = ""
  CONFIG["ARCH_FLAG"] = "#{ARCHFLAGS}"
  CONFIG["STATIC"] = ""
  CONFIG["CCDLFLAGS"] = "-fno-common"
  CONFIG["LDSHARED"] = "$(CC) -dynamic -bundle -undefined suppress -flat_namespace #{ARCHFLAGS}"
  CONFIG["LDSHAREDXX"] = "$(CXX) -dynamic -bundle -undefined suppress -flat_namespace"
  CONFIG["DLEXT"] = "bundle"
  CONFIG["DLEXT2"] = ""
  CONFIG["LIBEXT"] = "a"
  CONFIG["LINK_SO"] = ""
  CONFIG["LIBPATHFLAG"] = " -L%s"
  CONFIG["RPATHFLAG"] = ""
  CONFIG["LIBPATHENV"] = "DYLD_LIBRARY_PATH"
  CONFIG["TRY_LINK"] = ""
  CONFIG["STRIP"] = "strip -A -n"
  CONFIG["EXTSTATIC"] = ""
  CONFIG["setup"] = "Setup"
  CONFIG["PREP"] = "miniruby$(EXEEXT)"
  CONFIG["EXTOUT"] = ".ext"
  CONFIG["ARCHFILE"] = ""
  CONFIG["RDOCTARGET"] = "install-doc"
  CONFIG["cppflags"] = ""
  CONFIG["cflags"] = "$(optflags) $(debugflags) $(warnflags)"
  CONFIG["optflags"] = "-O2"
  CONFIG["debugflags"] = "-g"
  CONFIG["warnflags"] = "-Wall -Wno-parentheses"
  CONFIG["LIBRUBY_LDSHARED"] = "gcc -dynamiclib -undefined suppress -flat_namespace"
  CONFIG["LIBRUBY_DLDFLAGS"] = "-install_name $(libdir)/lib$(RUBY_SO_NAME).dylib -current_version $(MAJOR).$(MINOR).$(TEENY) -compatibility_version $(MAJOR).$(MINOR)"
  CONFIG["rubyw_install_name"] = ""
  CONFIG["RUBYW_INSTALL_NAME"] = ""
  CONFIG["LIBRUBY_A"] = "lib$(RUBY_SO_NAME)-static.a"
  CONFIG["LIBRUBY_SO"] = "lib$(RUBY_SO_NAME).$(MAJOR).$(MINOR).$(TEENY).dylib"
  CONFIG["LIBRUBY_ALIASES"] = "lib$(RUBY_SO_NAME).$(MAJOR).$(MINOR).dylib lib$(RUBY_SO_NAME).dylib"
  CONFIG["LIBRUBY"] = "$(LIBRUBY_SO)"
  CONFIG["LIBRUBYARG"] = "$(LIBRUBYARG_SHARED)"
  CONFIG["LIBRUBYARG_STATIC"] = "-l$(RUBY_SO_NAME)-static #{LDFLAGS}"
  CONFIG["LIBRUBYARG_SHARED"] = "-l$(RUBY_SO_NAME)"
  CONFIG["SOLIBS"] = ""
  CONFIG["DLDLIBS"] = ""
  CONFIG["ENABLE_SHARED"] = "yes"
  CONFIG["ENABLE_STATIC"] = "#{ENABLE_STATIC_LIBRARY}"
  CONFIG["MAINLIBS"] = ""
  CONFIG["COMMON_LIBS"] = ""
  CONFIG["COMMON_MACROS"] = ""
  CONFIG["COMMON_HEADERS"] = ""
  CONFIG["EXPORT_PREFIX"] = ""
  CONFIG["THREAD_MODEL"] = "pthread"
  CONFIG["MAKEFILES"] = "Makefile"
  CONFIG["arch"] = "#{NEW_RUBY_PLATFORM}"
  CONFIG["sitearch"] = "#{NEW_RUBY_PLATFORM}"
  CONFIG["sitedir"] = "$(libdir)/ruby/site_ruby"
  CONFIG["vendordir"] = "$(prefix)/lib/ruby/vendor_ruby"
  CONFIG["configure_args"] = ""
  CONFIG["rubyhdrdir"] = "$(includedir)/ruby-$(MAJOR).$(MINOR).$(TEENY)"
  CONFIG["sitehdrdir"] = "$(rubyhdrdir)/site_ruby"
  CONFIG["vendorhdrdir"] = "$(rubyhdrdir)/vendor_ruby"
  CONFIG["NROFF"] = "/usr/bin/nroff"
  CONFIG["MANTYPE"] = "doc"
  CONFIG["ruby_version"] = "$(MAJOR).$(MINOR).$(TEENY)"
  CONFIG["rubylibdir"] = "$(libdir)/ruby/$(ruby_version)"
  CONFIG["archdir"] = "$(rubylibdir)/$(arch)"
  CONFIG["sitelibdir"] = "$(sitedir)/$(ruby_version)"
  CONFIG["sitearchdir"] = "$(sitelibdir)/$(sitearch)"
  CONFIG["vendorlibdir"] = "$(vendordir)/$(ruby_version)"
  CONFIG["vendorarchdir"] = "$(vendorlibdir)/$(sitearch)"
  CONFIG["topdir"] = File.dirname(__FILE__)
  MAKEFILE_CONFIG = {}
  CONFIG.each{|k,v| MAKEFILE_CONFIG[k] = v.dup}
  def RbConfig::expand(val, config = CONFIG)
    val.gsub!(/\\$\\$|\\$\\(([^()]+)\\)|\\$\\{([^{}]+)\\}/) do
      var = $&
      if !(v = $1 || $2)
        '$'
      elsif key = config[v = v[/\\A[^:]+(?=(?::(.*?)=(.*))?\\z)/]]
        pat, sub = $1, $2
        config[v] = false
        RbConfig::expand(key, config)
        config[v] = key
        key = key.gsub(/\#{Regexp.quote(pat)}(?=\\s|\\z)/n) {sub} if pat
        key
      else
        var
      end
    end
    val
  end
  CONFIG.each_value do |val|
    RbConfig::expand(val)
  end
end
Config = RbConfig # compatibility for ruby-1.8.4 and older.
CROSS_COMPILING = nil
RUBY_FRAMEWORK = true
RUBY_FRAMEWORK_VERSION = RbConfig::CONFIG['ruby_version']
EOS
  if !File.exist?('rbconfig.rb') or File.read('rbconfig.rb') != rbconfig
    File.open('rbconfig.rb', 'w') { |io| io.print rbconfig }
  end
end

namespace :macruby do
  desc "Build dynamic libraries for MacRuby"
  task :dylib => [:rbconfig, :miniruby] do
    $stderr.puts "Warning: this version of MacRuby is still under development and can only build the \"miniruby\" target."
    system("ls -l ./miniruby")
    exit 0
    sh("./miniruby -I. -I./lib -rrbconfig tool/compile_prelude.rb prelude.rb gem_prelude.rb prelude.c.new")
    if !File.exist?('prelude.c') or File.read('prelude.c') != File.read('prelude.c.new')
      mv('prelude.c.new', 'prelude.c')
      $builder.build(['prelude'])
    else
      rm('prelude.c.new')
    end
    dylib = "lib#{RUBY_SO_NAME}.#{NEW_RUBY_VERSION}.dylib"
    $builder.link_dylib(dylib, $builder.objs - ['main', 'gc-stub', 'miniprelude'])
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
    $builder.link_archive("lib#{RUBY_SO_NAME}-static.a", $builder.objs - ['main', 'gc-stub', 'miniprelude'])
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
EXTMK_ARGS = "#{SCRIPT_ARGS} --extension --extstatic"
INSTRUBY_ARGS = "#{SCRIPT_ARGS} --data-mode=0644 --prog-mode=0755 --installed-list #{INSTALLED_LIST} --mantype=\"doc\" --sym-dest-dir=\"#{SYM_INSTDIR}\""

desc "Build extensions"
task :extensions => [:miniruby, "macruby:static"] do
  sh "./miniruby -I./lib -I.ext/common -I./- -r./ext/purelib.rb ext/extmk.rb #{EXTMK_ARGS}"
end

namespace :framework do
  desc "Create the plist file for the framework"
  task :info_plist do
    plist = <<EOS
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
        <key>CFBundleDevelopmentRegion</key>
        <string>English</string>
        <key>CFBundleExecutable</key>
        <string>Ruby</string>
        <key>CFBundleName</key>
        <string>Ruby</string>
        <key>CFBundleGetInfoString</key>
        <string>MacRuby Runtime and Library</string>
        <key>CFBundleIconFile</key>
        <string></string>
        <key>CFBundleIdentifier</key>
        <string>com.apple.macruby</string>
        <key>CFBundleInfoDictionaryVersion</key>
        <string>#{MACRUBY_VERSION}</string>
        <key>CFBundlePackageType</key>
        <string>FMWK</string>
        <key>CFBundleShortVersionString</key>
        <string>#{MACRUBY_VERSION}</string>
        <key>CFBundleSignature</key>
        <string>????</string>
        <key>CFBundleVersion</key>
        <string>MacRuby-#{MACRUBY_VERSION}</string>
        <key>NSPrincipalClass</key>
        <string></string>
</dict>
</plist>
EOS
    File.open('framework/Info.plist', 'w') { |io| io.print plist }
  end

  desc "Install the framework"
  task :install => :info_plist do
    $stderr.puts "This version of MacRuby is under development and cannot be installed yet. Please retry later."
    exit 1
    sh "./miniruby instruby.rb #{INSTRUBY_ARGS}"
  end
end

namespace :clean do
  desc "Clean local build files"
  task :local do
    $builder.clean
    ['parse.c', 'lex.c', INSTALLED_LIST, 'Makefile', *Dir['*.inc']].each { |x| rm_f(x) }
  end

  desc "Clean extension build files"
  task :ext do
    if File.exist?('./miniruby') 
      sh "./miniruby -I./lib -I.ext/common -I./- -r./ext/purelib.rb ext/extmk.rb #{EXTMK_ARGS} -- clean"
    end
  end
end
