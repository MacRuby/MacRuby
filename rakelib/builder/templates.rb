class Builder
  def self.create_framework_info_plist
    plist = <<EOS
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
        <key>CFBundleDevelopmentRegion</key>
        <string>English</string>
        <key>CFBundleExecutable</key>
        <string>MacRuby</string>
        <key>CFBundleName</key>
        <string>MacRuby</string>
        <key>CFBundleGetInfoString</key>
        <string>MacRuby Runtime and Library</string>
        <key>CFBundleIconFile</key>
        <string></string>
        <key>CFBundleIdentifier</key>
        <string>org.macruby</string>
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

  def self.create_rbconfig
    archflags = ARCHS.map { |x| "-arch #{x}" }.join(' ')
    rbconfig = <<EOS
# This file was created when MacRuby was built.  Any changes made to this file
# will be lost the next time MacRuby is built.

module RbConfig
  RUBY_VERSION == "#{NEW_RUBY_VERSION}" or
    raise "ruby lib version (#{NEW_RUBY_VERSION}) doesn't match executable version (\#{RUBY_VERSION})"

  TOPDIR = File.dirname(__FILE__).chomp!("/lib/ruby/#{NEW_RUBY_VERSION}/#{NEW_RUBY_PLATFORM}")
  DESTDIR = '' unless defined? DESTDIR
  ARCHFLAGS =
    if e = ENV['ARCHFLAGS']
      e.dup
    else
      "#{archflags}"
    end
  CONFIG = {}
  CONFIG["DESTDIR"] = DESTDIR
  CONFIG["INSTALL"] = '/usr/bin/install -c'
  CONFIG["prefix"] = (TOPDIR || DESTDIR + "#{FRAMEWORK_USR}")
  CONFIG["INSTALL_VERSION"] = "#{INSTALL_VERSION}"
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
  CONFIG["symdir"] = "#{SYM_INSTDIR}"
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
  if File.exist? "#{CC}"
    CONFIG["CC"] = "#{CC}"
  else
    CONFIG["CC"] = "/usr/bin/gcc"
  end
  CONFIG["CFLAGS"] = "$(ARCH_FLAG) -fexceptions -fno-common -pipe $(cflags)"
  CONFIG["LDFLAGS"] = "$(ARCH_FLAG)"
  CONFIG["CPPFLAGS"] = "$(cppflags)"
  CONFIG["OBJEXT"] = "o"
  if File.exist? "#{CXX}"
    CONFIG["CXX"] = "#{CXX}"
  else
    CONFIG["CXX"] = "/usr/bin/g++"
  end
  CONFIG["CXXFLAGS"] = "$(ARCH_FLAG)"
  CONFIG["CPP"] = "/usr/bin/gcc -E"
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
  CONFIG["ARCH_FLAG"] = ARCHFLAGS
  CONFIG["STATIC"] = ""
  CONFIG["CCDLFLAGS"] = "-fno-common"
  CONFIG["LDSHARED"] = "$(CC) -dynamic -bundle -undefined suppress -flat_namespace $(ARCH_FLAG)"
  CONFIG["LDSHAREDXX"] = "$(CXX) -dynamic -bundle -undefined suppress -flat_namespace $(ARCH_FLAG)"
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
  CONFIG["optflags"] = "-O#{OPTZ_LEVEL}"
  CONFIG["debugflags"] = "-g"
  CONFIG["warnflags"] = "-Wall"
  CONFIG["LIBRUBY_LDSHARED"] = "/usr/bin/gcc -dynamiclib -undefined suppress -flat_namespace $(ARCH_FLAG)"
  CONFIG["LIBRUBY_DLDFLAGS"] = "-install_name $(libdir)/lib$(RUBY_SO_NAME).dylib -current_version $(MAJOR).$(MINOR).$(TEENY) -compatibility_version $(MAJOR).$(MINOR)"
  CONFIG["rubyw_install_name"] = ""
  CONFIG["RUBYW_INSTALL_NAME"] = ""
  CONFIG["LIBRUBY_A"] = "lib$(RUBY_SO_NAME)-static.a"
  CONFIG["LIBRUBY_SO"] = "lib$(RUBY_SO_NAME).$(MAJOR).$(MINOR).$(TEENY).dylib"
  CONFIG["LIBRUBY_ALIASES"] = "#{DYLIB_ALIASES}"
  CONFIG["LIBRUBY"] = "$(LIBRUBY_SO)"
  CONFIG["LIBRUBYARG"] = "$(LIBRUBYARG_SHARED)"
  CONFIG["LIBRUBYARG_STATIC_REALLY"] = "-l$(RUBY_SO_NAME)-static"
  CONFIG["LIBRUBYARG_SHARED"] = CONFIG["LIBRUBYARG_STATIC"] = "-l$(RUBY_SO_NAME)"
  CONFIG["SOLIBS"] = ""
  CONFIG["DLDLIBS"] = ""
  CONFIG["ENABLE_SHARED"] = "yes"
  CONFIG["ENABLE_STATIC"] = "no"
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
  CONFIG["LLVM_PATH"] = "#{LLVM_PATH}"
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
EOS
    if !File.exist?('rbconfig.rb') or File.read('rbconfig.rb') != rbconfig
      File.open('rbconfig.rb', 'w') { |io| io.print rbconfig }
    end
  end
end
