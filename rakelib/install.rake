require 'fileutils'

# INSTRUBY_ARGS = "#{SCRIPT_ARGS}
# --installed-list #{INSTALLED_LIST} --mantype=\"doc\" --sym-dest-dir=\"#{SYM_INSTDIR}\"

module Installer
  include FileUtils

  # Hard coded
  def prog_mode;   0755;   end
  def dir_mode;    0755;   end
  def data_mode;   0644;   end
  def script_mode; 0775;   end
  def man_type;    'doc'; end
  def rdoc_dir;    'doc/'; end

  def with_destdir dir
    return dir if !DESTDIR or DESTDIR.empty?
    DESTDIR + dir
  end

  def ln_sf src, dest
    super(src, with_destdir(dest))
    puts dest
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

  def mkdir_p target, *flags
    super(with_destdir(target), *flags)
  end

  def xcode_dir
    @xcode_dir ||= `xcode-select -print-path`.chomp
  end

  def install_misc what, from, to
    puts "Installing #{what}"
    mkdir_p to, :mode => dir_mode
    install_recursive from, to, :mode => dir_mode
  end

end


namespace :install do
  extend Installer

  task :all => [:resources, :bin, :lib, :ext, :headers, :doc, :xcode_support]

  desc 'Install the MacRuby.framework Info.plist file'
  task :resources => 'framework:info_plist' do
    mkdir_p FRAMEWORK_RESOURCES, :mode => 0755
    install File.join('framework/Info.plist'), FRAMEWORK_RESOURCES, :mode => 0644
  end

  desc 'Install MacRuby binaries'
  task :bin do
    puts 'Installing the macruby binary command'

    arch_lib_dir = File.join(FRAMEWORK_USR_LIB, 'ruby', NEW_RUBY_VERSION, NEW_RUBY_PLATFORM)
    dylib        = "lib#{RUBY_SO_NAME}.#{NEW_RUBY_VERSION}.dylib"
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
    lib_dir = File.join(FRAMEWORK_USR_LIB, 'ruby', NEW_RUBY_VERSION)

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
    install_misc 'Xcode 4.x templates', 'misc/xcode4-templates',
                 "#{xcode_dir}/Library/Xcode/Templates"
    install_misc 'Xcode 3.x templates', 'misc/xcode-templates',
                 '/Library/Application Support/Developer/3.0/Xcode'
  end

  task :xcode_samples do
    install_misc 'MacRuby sample projects', 'sample-macruby',
                 "#{xcode_dir}/Examples/Ruby/MacRuby"
  end

end
