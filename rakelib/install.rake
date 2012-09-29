require 'fileutils'

# TODO abstract usage of with_destdir out of the rake tasks
#      so that they only appear in helper methods

module Installer
  include FileUtils

  # Hard coded
  def prog_mode;   0755; end
  def dir_mode;    0755; end
  def data_mode;   0644; end
  def script_mode; 0775; end

  def made_dirs
    @made_dirs ||= []
  end

  def with_destdir dir
    return dir if !DESTDIR or DESTDIR.empty?
    return DESTDIR + dir
  end

  def ln_sf src, dest
    super(src, with_destdir(dest))
    # puts dest
  end

  def ln_sfh src, dest
    ln_sf(src, dest) unless File.symlink?(with_destdir(dest))
  end

  def install src, dest, opts = {}
    strip = opts.delete :strip
    opts[:preserve] = true
    super(src, with_destdir(dest), opts)
    if made_dirs.include? dest
      dest = File.join(dest, File.basename(src))
    end
    #puts dest
    if strip
      system("/usr/bin/strip -x '#{File.join(with_destdir(dest), File.basename(src))}'")
    end
  end

  def makedirs *dirs
    dirs.collect! do |dir|
      realdir = with_destdir(dir)
      realdir unless made_dirs.include?(dir) do
        made_dirs << dir
        # puts File.join(dir, '')
        File.directory?(realdir)
      end
    end.compact!
    super(dirs, :mode => dir_mode) unless dirs.empty?
  end

  def install_recursive srcdir, dest, options = {}
    opts    = options.clone
    noinst  = Array(opts.delete(:no_install))
    glob    = opts.delete(:glob) || '*'
    subpath = srcdir.size..-1
    Dir.glob("#{srcdir}/**/#{glob}") do |src|
      base  = File.basename(src)
      next if base.match(/\A\#.*\#\z/) or base.match(/~\z/)
      next if noinst.any? { |n| File.fnmatch?(n, base) }
      d = dest + src[subpath]
      if File.directory?(src)
        makedirs(d)
      else
        makedirs(File.dirname(d))
        install src, d, opts
      end
    end
  end

  def open_for_install path, mode
    data = begin
             realpath = with_destdir(path)
             open(realpath, 'rb') { |f| f.read }
           rescue
             nil
           end
    newdata = yield
    unless newdata == data
      open(realpath, 'wb', mode) { |f| f.write newdata }
    end
    File.chmod(mode, realpath)
    puts 'Wrote ' + path
  end

  def dylib
    "lib#{RUBY_SO_NAME}.#{NEW_RUBY_VERSION}.dylib"
  end

  def ruby_shebang
    "#{FRAMEWORK_USR_BIN}/#{RUBY_INSTALL_NAME}"
  end

  def man_dir
    "#{FRAMEWORK_USR_SHARE}/man"
  end

  def ri_dir
    "#{FRAMEWORK_USR_SHARE}/ri/#{NEW_RUBY_VERSION}/system"
  end

  def lib_dir
    "#{FRAMEWORK_USR_LIB}/ruby/#{NEW_RUBY_VERSION}"
  end

  def arch_lib_dir
    "#{lib_dir}/#{NEW_RUBY_PLATFORM}"
  end

  def header_dir
    "#{FRAMEWORK_USR}/include/ruby-#{NEW_RUBY_VERSION}"
  end

  def ruby_header_dir
    "#{header_dir}/ruby"
  end

  def arch_header_dir
    "#{header_dir}/#{NEW_RUBY_PLATFORM}"
  end

  def dest_bin
    "#{SYM_INSTDIR}/bin"
  end

  def dest_man
    "#{SYM_INSTDIR}/share/man"
  end

end


namespace :install do
  extend Installer

  task :all => [:standard, :xcode_support]

  desc 'Install MacRuby without Xcode support'
  task :standard => [:bin, :scripts, :lib, :ext, :headers, :doc, :man, :resources]

  desc 'Install MacRuby binaries'
  task :bin do
    puts 'Installing the macruby binary and dependencies'

    makedirs FRAMEWORK_USR_BIN, FRAMEWORK_USR_LIB, arch_lib_dir

    install RUBY_INSTALL_NAME, FRAMEWORK_USR_BIN, :mode => prog_mode, :strip => true
    install 'rbconfig.rb',  arch_lib_dir,         :mode => data_mode
    install 'rbconfig.rbo', arch_lib_dir,         :mode => data_mode
    install dylib,          FRAMEWORK_USR_LIB,    :mode => prog_mode, :strip => true
    for link in DYLIB_ALIASES.split
      ln_sf dylib, "#{FRAMEWORK_USR_LIB}/#{link}"
    end

    puts 'Installing LLVM tool'
    install "#{LLVM_PATH}/bin/llc", "#{FRAMEWORK_USR_BIN}/llc", :mode => prog_mode
  end

  desc 'Install command scripts (e.g. macirb)'
  task :scripts do
    puts 'Installing command scripts'

    makedirs FRAMEWORK_USR_BIN

    for src in Dir.glob('bin/*')
      next unless File.file?(src)
      next if /\/[.#]|(\.(old|bak|orig|rej|diff|patch|core)|~|\/core)$/i =~ src

      bname = File.basename(src)
      name  = case bname
              when 'rb_nibtool'
                bname
              else
                RUBY_INSTALL_NAME.sub(/ruby/, bname)
              end

      shebang = ''
      body = ''
      open(src, 'rb') do |f|
        shebang = f.gets
        body = f.read
      end
      shebang.sub!(/^\#!.*?ruby\b/) { '#!' + ruby_shebang }
      shebang.sub!(/\r$/, '')
      body.gsub!(/\r$/, '')

      cmd = "#{FRAMEWORK_USR_BIN}/#{name}"
      open_for_install(cmd, script_mode) do
        shebang + body
      end
    end
  end

  desc 'Install the standard library'
  task :lib do
    makedirs lib_dir

    for file in Dir['lib/**/*{.rb,.rbo,help-message}']
      dir = File.dirname(file).sub!(/\Alib/, lib_dir) || lib_dir
      makedirs dir
      install file, dir, :mode => data_mode
    end
  end

  desc 'Install the C extensions'
  task :ext do
    Builder::Ext.install
    # Install the extensions rbo.
    Dir.glob('ext/**/lib/**/*.rbo').each do |path|
      ext_name, sub_path = path.scan(/^ext\/(.+)\/lib\/(.+)$/)[0]
      next unless EXTENSIONS.include?(ext_name)
      sub_dir = File.dirname(sub_path)
      install_recursive path, "#{RUBY_SITE_LIB2}/#{sub_dir}", :mode => prog_mode
    end

    puts 'Installing extension objects'
    makedirs RUBY_SITE_LIB2, RUBY_VENDOR_ARCHLIB
    install_recursive "#{EXTOUT}/#{NEW_RUBY_PLATFORM}", arch_lib_dir, :mode => prog_mode
    install_recursive "#{EXTOUT}/include/#{NEW_RUBY_PLATFORM}", arch_header_dir, :glob => '*.h', :mode => data_mode

    puts 'Installing extensions scripts'
    install_recursive "#{EXTOUT}/common", lib_dir, :mode => data_mode
    install_recursive "#{EXTOUT}/include/ruby", ruby_header_dir, :glob => '*.h', :mode => data_mode
  end

  desc 'Install the MacRuby headers'
  task :headers do
    puts 'Installing headers'
    install_recursive 'include', header_dir, :glob => '*.h', :mode => data_mode
  end

  desc 'Install RDoc and RI documentation'
  task :doc do
    puts 'Installing RI data'
    install_recursive 'doc/', ri_dir, :mode => data_mode
  end

  desc 'Install the MacRuby man pages'
  task :man do
    puts 'Installing man pages'
    for mdoc in Dir['*.[1-9]']
      # TODO is this check really needed?
      next unless File.file?(mdoc) and open(mdoc){ |fh| fh.read(1) == '.' }

      destdir  = "#{man_dir}/man#{mdoc[/(\d+)$/]}"
      destfile = "#{destdir}/#{mdoc.sub(/^/, 'mac')}"

      makedirs destdir
      install mdoc, destfile, :mode => data_mode
    end
  end

  task :resources do
  desc 'Install the MacRuby framework and related resources'
    puts 'Installing framework resources'

    makedirs FRAMEWORK_RESOURCES
    install 'framework/Info.plist', FRAMEWORK_RESOURCES, :mode => data_mode

    resources = "#{FRAMEWORK_RESOURCES}/English.lproj"
    makedirs resources
    install 'framework/InfoPlist.strings', resources, :mode => data_mode
    if File.symlink?(with_destdir("#{FRAMEWORK_VERSION}/../Current"))
      rm_f "#{FRAMEWORK_VERSION}/../Current"
    end

    ln_sfh INSTALL_VERSION.to_s,                   "#{FRAMEWORK_VERSION}/../Current"
    ln_sfh 'Versions/Current/Headers',             "#{FRAMEWORK_VERSION}/../../Headers"
    ln_sfh 'Versions/Current/MacRuby',             "#{FRAMEWORK_VERSION}/../../MacRuby"
    ln_sfh 'Versions/Current/Resources',           "#{FRAMEWORK_VERSION}/../../Resources"
    ln_sfh "usr/lib/#{dylib}",                     "#{FRAMEWORK_VERSION}/MacRuby"
    ln_sfh "usr/include/ruby-#{NEW_RUBY_VERSION}", "#{FRAMEWORK_VERSION}/Headers"
    ln_sfh "../#{NEW_RUBY_PLATFORM}/ruby/config.h",
      "#{FRAMEWORK_VERSION}/usr/include/ruby-#{NEW_RUBY_VERSION}/ruby/config.h"

    puts 'Installing executable symlinks'
    makedirs dest_bin
    Dir.entries(with_destdir(FRAMEWORK_USR_BIN)).each do |file|
      next if file.match(/^\./)
      # Except rb_nibtool & llc!
      next if file == 'rb_nibtool' or file == 'llc'
      link = "#{FRAMEWORK_USR_BIN}/#{file}"
      link.sub!(/#{INSTALL_VERSION}/, 'Current')
      link_dest = "#{dest_bin}/#{File.basename(file)}"
      unless File.exists?(with_destdir(link_dest))
        ln_sfh link, link_dest
      end
    end

    puts 'Installing man page symlinks'
    makedirs dest_man
    Dir.entries(with_destdir(man_dir)).each do |man_set|
      next if man_set.match(/^\./)
      if File.stat("#{with_destdir(man_dir)}/#{man_set}").directory?
        makedirs "#{dest_man}/#{File.basename(man_set)}"
        Dir.entries("#{with_destdir(man_dir)}/#{man_set}").each do |man_file|
          next if man_file.match(/^\./)
          link = "../../../../../#{man_dir}/#{man_set}/#{man_file}"
          link.sub!(/#{INSTALL_VERSION}/, 'Current')
          link_dest = "#{dest_man}/#{File.basename(man_set)}/#{File.basename(man_file)}"
          unless File.exists?(with_destdir(link_dest))
            ln_sfh link, link_dest
          end
        end
      else
        link = "../../../../#{man_dir}/#{man_set}"
        link.sub!(/#{INSTALL_VERSION}/, 'Current')
        ln_sfh link, "#{dest_man}/#{File.basename(man_set)}"
      end
    end

    install_recursive 'misc/xcode4-templates', "#{FRAMEWORK_RESOURCES}/Templates", :mode => prog_mode
  end

  desc 'Install all Xcode related things'
  task :xcode_support do
    `/usr/local/bin/macruby_install_xcode_support`
  end

end
