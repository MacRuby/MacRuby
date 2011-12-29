require 'fileutils'

# INSTRUBY_ARGS = "#{SCRIPT_ARGS} --data-mode=0644
# --installed-list #{INSTALLED_LIST} --mantype=\"doc\" --sym-dest-dir=\"#{SYM_INSTDIR}\"
# --rdoc-output=\"doc\""

module Installer
  include FileUtils

  # Hard coded
  PROG_MODE = 0775
  DIR_MODE  = PROG_MODE

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
    install(src, with_destdir(dest), opts)
    dest = File.join(dest, File.basename(src)) if $made_dirs[dest]
    puts dest
    if strip
      system("/usr/bin/strip -x \"#{with_destdir(dest)}\"")
    end
  end

  def made_dirs
    @made_dirs ||= []
  end

  def makedirs dirs
    dirs = fu_list(dirs)
    dirs.collect! do |dir|
      realdir = with_destdir(dir)
      realdir unless made_dirs.include?(dir) do
        made_dirs << dir
        puts File.join(dir, '')
        File.directory?(realdir)
      end
    end.compact!
    super(dirs, :mode => DIR_MODE) unless dirs.empty?
  end

  def install_recursive srcdir, dest, options = {}
    opts = options.clone
    noinst = opts.delete(:no_install)
    glob = opts.delete(:glob) || '*'
    subpath = srcdir.size..-1
    Dir.glob("#{srcdir}/**/#{glob}") do |src|
      case base = File.basename(src)
      when /\A\#.*\#\z/, /~\z/
        next
      end
      if noinst
        if Array === noinst
          next if noinst.any? {|n| File.fnmatch?(n, base)}
        else
          next if File.fnmatch?(noinst, base)
        end
      end
      d = dest + src[subpath]
      if File.directory?(src)
        makedirs(d)
      else
        makedirs(File.dirname(d))
        install src, d, opts
      end
    end
  end

  def xcode_dir
    @xcode_dir ||= `xcode-select -print-path`.chomp
  end

  def install_stuff what, from, to, mode
    puts "installing #{what}"
    mkdir_p to, :mode => mode
    install_recursive from, to, :mode => mode
  end

end


namespace :install do
  extend Installer

  task :all => [:info_plist, :ext, :nibtool]

  desc 'Install the MacRuby.framework Info.plist file'
  task :resources => 'framework:info_plist' do
    mkdir_p FRAMEWORK_RESOURCES, :mode => 0755
    install File.join('framework/Info.plist'), FRAMEWORK_RESOURCES, :mode => 0644
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

  desc 'Install MacRuby support for Interface Builder'
  task :nibtool do
    puts 'Installing IB support'
    ib_dest = "#{xcode_dir}/usr/bin"
    mkdir_p ib_dest
    ln_sfh File.join('../../..', FRAMEWORK_USR_BIN, 'rb_nibtool'), ib_dest
  end

  task :xcode_templates do
  end

end
