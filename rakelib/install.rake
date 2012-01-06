require 'fileutils'

# TODO Centralize most, if not all, the directory creation

module Installer
  include FileUtils

  # Hard coded
  def prog_mode;   0755;   end
  def dir_mode;    0755;   end
  def data_mode;   0644;   end
  def script_mode; 0775;   end
  def man_type;    'doc';  end
  def rdoc_dir;    'doc/'; end

  # TODO I think this might be useless, even if DESTDIR is set
  def with_destdir dir
    return dir if !DESTDIR or DESTDIR.empty?
    DESTDIR + dir
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
      system("/usr/bin/strip -x \"#{with_destdir(dest)}\"")
    end
  end

  def made_dirs
    @made_dirs ||= []
  end

  def makedirs *dirs
    dirs = fu_list(dirs.flatten)
    dirs.collect! do |dir|
      realdir = with_destdir(dir)
      realdir unless made_dirs.include?(dir) do
        made_dirs << dir
        puts File.join(dir, '')
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

  def mkdir_p target, flags = {}
    flags[:mode] = dir_mode
    super(with_destdir(target), flags)
  end

  def xcode_dir
    `xcode-select -print-path`.chomp
  end

  def man_dir
    File.join(FRAMEWORK_USR_SHARE, 'man')
  end

  def dylib
    "lib#{RUBY_SO_NAME}.#{NEW_RUBY_VERSION}.dylib"
  end

  def lib_dir
    File.join(FRAMEWORK_USR_LIB, 'ruby', NEW_RUBY_VERSION)
  end

end


namespace :install do
  extend Installer

  task :all => [:bin, :lib, :ext, :headers, :doc, :man, :resources, :xcode_support]

  desc 'Install MacRuby binaries'
  task :bin do
    puts 'Installing the macruby binary command'

    arch_lib_dir = File.join(FRAMEWORK_USR_LIB, 'ruby', NEW_RUBY_VERSION, NEW_RUBY_PLATFORM)
    static       = "lib#{RUBY_SO_NAME}-static.a"

    makedirs FRAMEWORK_USR_BIN, FRAMEWORK_USR_LIB, arch_lib_dir

    install RUBY_INSTALL_NAME, FRAMEWORK_USR_BIN, :mode => prog_mode, :strip => true
    install 'rbconfig.rb',  arch_lib_dir,         :mode => data_mode
    install 'rbconfig.rbo', arch_lib_dir,         :mode => data_mode
    install dylib,          FRAMEWORK_USR_LIB,    :mode => prog_mode, :strip => true
    if File.exists?(static)
      install static,       FRAMEWORK_USR_LIB,    :mode => data_mode, :strip => true
    end
    for link in DYLIB_ALIASES.split
      ln_sf(dylib, File.join(FRAMEWORK_USR_LIB, link))
    end

    puts 'Installing LLVM tools'
    llc_dest = File.join(FRAMEWORK_USR_BIN, 'llc')
    llc_src  = File.join(LLVM_PATH, 'bin/llc')
    install(llc_src, llc_dest, :mode => prog_mode)
  end

  desc 'Install the standard library'
  task :lib => 'stdlib:build' do
    makedirs lib_dir

    for file in Dir['lib/**/*{.rb,.rbo,help-message}']
      dir = File.dirname(file).sub!(/\Alib/, lib_dir) || lib_dir
      makedirs dir
      install file, dir, :mode => data_mode
    end
  end

  desc 'Install the C extensions'
  task :ext => :extensions do
    Builder::Ext.install
    # Install the extensions rbo.
    dest_site = File.join(DESTDIR, RUBY_SITE_LIB2)
    Dir.glob('ext/**/lib/**/*.rbo').each do |path|
      ext_name, sub_path = path.scan(/^ext\/(.+)\/lib\/(.+)$/)[0]
      next unless EXTENSIONS.include?(ext_name)
      sub_dir = File.dirname(sub_path)
      sh "/usr/bin/install -c -m 0755 #{path} #{File.join(dest_site, sub_dir)}"
    end
  end

  desc 'Install the MacRuby headers'
  task :headers do
    puts 'Installing headers'
    header_dir = "#{FRAMEWORK_USR}/include/ruby-#{NEW_RUBY_VERSION}"
    makedirs header_dir
    install_recursive('include', header_dir, :glob => "*.h", :mode => data_mode)
  end

  desc 'Install RDoc and RI documentation'
  task :doc => 'rake:doc' do
    puts 'Installing RDoc and RI'
    ridatadir = File.join(FRAMEWORK_USR_SHARE, "ri/#{NEW_RUBY_VERSION}/system")
    makedirs ridatadir
    install_recursive(rdoc_dir, ridatadir, :mode => data_mode)
  end

  desc 'Install the MacRuby man pages'
  task :man do
    puts 'Installing man pages'

    for mdoc in Dir['*.[1-9]']
      # TODO is this check really needed?
      next unless File.file?(mdoc) and open(mdoc){ |fh| fh.read(1) == '.' }

      destdir  = File.join(man_dir, "man#{mdoc[/(\d+)$/]}")
      destfile = File.join(destdir, mdoc.sub(/ruby/, RUBY_INSTALL_NAME))

      makedirs destdir
      install mdoc, destfile, :mode => data_mode
    end
  end

  desc 'Install the MacRuby.framework Info.plist file'
  task :resources => 'framework:info_plist' do
    puts 'Installing framework' # TODO Make this sound like a natural sentence...

    mkdir_p FRAMEWORK_RESOURCES
    install File.join('framework/Info.plist'), FRAMEWORK_RESOURCES, :mode => data_mode

    resources = File.join(FRAMEWORK_RESOURCES, 'English.lproj')
    mkdir_p resources
    install File.join('framework/InfoPlist.strings'), resources, :mode => data_mode
    if File.symlink?(with_destdir(File.join(FRAMEWORK_VERSION, '..', 'Current')))
      rm_f File.join(FRAMEWORK_VERSION, '..', 'Current')
    end

    ln_sfh INSTALL_VERSION.to_s,                   File.join(FRAMEWORK_VERSION, '..', 'Current')
    ln_sfh 'Versions/Current/Headers',             File.join(FRAMEWORK_VERSION, '../../Headers')
    ln_sfh 'Versions/Current/MacRuby',             File.join(FRAMEWORK_VERSION, '../../MacRuby')
    ln_sfh 'Versions/Current/Resources',           File.join(FRAMEWORK_VERSION, '../../Resources')
    ln_sfh "usr/lib/#{dylib}",                     File.join(FRAMEWORK_VERSION, 'MacRuby')
    ln_sfh "usr/include/ruby-#{NEW_RUBY_VERSION}", File.join(FRAMEWORK_VERSION, 'Headers')
    ln_sfh "../#{NEW_RUBY_PLATFORM}/ruby/config.h",
      File.join(FRAMEWORK_VERSION, "usr/include/ruby-#{NEW_RUBY_VERSION}/ruby/config.h")

    puts 'Installing executable links'
    dest_bin = File.join(SYM_INSTDIR, 'bin')
    mkdir_p dest_bin
    Dir.entries(with_destdir(FRAMEWORK_USR_BIN)).each do |file|
      next if file.match(/^./)
      # Except rb_nibtool & llc!
      next if file == 'rb_nibtool' or file == 'llc'
      link = File.join('../../../', FRAMEWORK_USR_BIN, file)
      link.sub!(/#{INSTALL_VERSION}/, 'Current')
      link_dest = File.join(dest_bin, File.basename(file))
      unless File.exists?(link_dest)
        ln_sfh link, link_dest
      end
    end

    puts 'Installing man page links'
    dest_man = File.join(SYM_INSTDIR, 'share', 'man')
    mkdir_p dest_man
    Dir.entries(with_destdir(man_dir)).each do |man_set|
      next if man_set.match(/^./)
      if File.stat(File.join(with_destdir(man_dir), man_set)).directory?
        mkdir_p File.join(dest_man, File.basename(man_set))
        Dir.entries(File.join(with_destdir(man_dir), man_set)).each do |man_file|
          next if man_file.match(/^./)
          link = File.join('../../../../../', man_dir, man_set, man_file)
          link.sub!(/#{INSTALL_VERSION}/, 'Current')
          link_dest = File.join(dest_man, File.basename(man_set), File.basename(man_file))
          unless File.exists?(link_dest)
            ln_sfh link, link_dest
          end
        end
      else
        link = File.join('../../../../', man_dir, man_set)
        link.sub!(/#{INSTALL_VERSION}/, 'Current')
        ln_sfh link, File.join(dest_man, File.basename(man_set))
      end
    end
  end

  desc 'Install all Xcode related things'
  task :xcode_support => [:nibtool, :xcode_templates, :xcode_samples]

  task :nibtool do
    puts 'Installing IB support'
    ib_dest = "#{xcode_dir}/usr/bin"
    mkdir_p ib_dest
    ln_sfh File.join('../../..', FRAMEWORK_USR_BIN, 'rb_nibtool'), ib_dest
  end

  task :xcode_templates do
    # TODO only install templates for installed Xcodes
    puts 'Installing XCode templates'

    xcode4 = "#{xcode_dir}/Library/Xcode/Templates"
    mkdir_p xcode4
    install_recursive 'misc/xcode4-templates', xcode4, :mode => prog_mode

    xcode3 = '/Library/Application Support/Developer/3.0/Xcode'
    mkdir_p xcode3
    install_recursive 'misc/xcode-templates', xcode3, :mode => prog_mode
  end

  task :xcode_samples do
    puts 'Installing MacRuby sample projects'
    dir = "#{xcode_dir}/Examples/Ruby/MacRuby"
    mkdir_p dir
    install_recursive 'sample-macruby', dir, :mode => script_mode
  end

end
