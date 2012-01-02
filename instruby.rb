$:.clear
$:.unshift File.expand_path("lib", srcdir)
require 'tempfile'

File.umask(0)

def parse_args(argv = ARGV)
  $destdir = $sym_destdir = nil
  $install = []
  $installed_list = nil
  $script_mode = nil
  $cmdtype = nil
  mflags = []
  opt = OptionParser.new
  opt.on('--dest-dir=DIR') {|dir| $destdir = dir}
  opt.on('--sym-dest-dir=DIR') {|dir| $sym_destdir = dir}
  opt.on('-i', '--install=TYPE',
         [:local, :bin, :"bin-arch", :"bin-comm", :lib, :man, :ext, :"ext-arch", :"ext-comm", :rdoc, :data]) do |ins|
    $install << ins
  end
  opt.on('--installed-list [FILENAME]') {|name| $installed_list = name}
  opt.on('--cmd-type=TYPE', %w[cmd plain]) {|cmd| $cmdtype = (cmd unless cmd == 'plain')}

  opt.order!(argv) do |v|
    case v
    when /\AINSTALL[-_]([-\w]+)=(.*)/
      argv.unshift("--#{$1.tr('_', '-')}=#{$2}")
    when /\A\w[-\w+]*=\z/
      mflags << v
    when /\A\w[-\w+]*\z/
      $install << v.intern
    else
      raise OptionParser::InvalidArgument, v
    end
  end rescue abort [$!.message, opt].join("\n")

  $make, *rest = Shellwords.shellwords($make)
  $mflags.unshift(*rest) unless rest.empty?
  $mflags.unshift(*mflags)

  def $mflags.set?(flag)
    grep(/\A-(?!-).*#{flag.chr}/i) { return true }
    false
  end
  def $mflags.defined?(var)
    grep(/\A#{var}=(.*)/) {return block_given? ? yield($1) : $1}
    false
  end

  if $mflags.set?(?n)
    $dryrun = true
  else
    $mflags << '-n' if $dryrun
  end

  $destdir ||= $mflags.defined?("DESTDIR")
  if $extout ||= $mflags.defined?("EXTOUT")
    Config.expand($extout)
  end

  $continue = $mflags.set?(?k)

  if $installed_list ||= $mflags.defined?('INSTALLED_LIST')
    Config.expand($installed_list, Config::CONFIG)
    $installed_list = open($installed_list, "ab")
    $installed_list.sync = true
  end

end

$install_procs = Hash.new {[]}
def install?(*types, &block)
  $install_procs[:all] <<= block
  types.each do |type|
    $install_procs[type] <<= block
  end
end

def open_for_install(path, mode)
  data = open(realpath = with_destdir(path), "rb") {|f| f.read} rescue nil
  newdata = yield
  unless $dryrun
    unless newdata == data
      open(realpath, "wb", mode) {|f| f.write newdata}
    end
    File.chmod(mode, realpath)
  end
  $installed_list.puts path if $installed_list
end

version = CONFIG["ruby_version"]
datadir = CONFIG['datadir']
archhdrdir = rubyhdrdir = CONFIG["rubyhdrdir"]
archhdrdir += "/" + CONFIG["arch"]
sitelibdir = CONFIG["sitelibdir"]
sitearchlibdir = CONFIG["sitearchdir"]
vendorlibdir = CONFIG["vendorlibdir"]
vendorarchlibdir = CONFIG["vendorarchdir"]
configure_args = Shellwords.shellwords(CONFIG["configure_args"])

if $extout
  extout = "#$extout"
  install?(:ext, :arch, :'ext-arch') do
    puts "installing extension objects"
    makedirs [archlibdir, sitearchlibdir, vendorarchlibdir, archhdrdir]
    if noinst = CONFIG["no_install_files"] and noinst.empty?
      noinst = nil
    end
    install_recursive("#{extout}/#{CONFIG['arch']}", archlibdir, :no_install => noinst, :mode => $prog_mode)
    install_recursive("#{extout}/include/#{CONFIG['arch']}", archhdrdir, :glob => "*.h", :mode => $data_mode)
  end
  install?(:ext, :comm, :'ext-comm') do
    puts "installing extension scripts"
    hdrdir = rubyhdrdir + "/ruby"
    makedirs [rubylibdir, sitelibdir, vendorlibdir, hdrdir]
    install_recursive("#{extout}/common", rubylibdir, :mode => $data_mode)
    install_recursive("#{extout}/include/ruby", hdrdir, :glob => "*.h", :mode => $data_mode)
  end
end

install?(:local, :comm, :bin, :'bin-comm') do
  puts "installing command scripts"

  Dir.chdir srcdir
  makedirs [bindir, rubylibdir]

  ruby_shebang = File.join(bindir, ruby_install_name)
  if File::ALT_SEPARATOR
    ruby_bin = ruby_shebang.tr(File::SEPARATOR, File::ALT_SEPARATOR)
  end
  for src in Dir["bin/*"]
    next unless File.file?(src)
    next if /\/[.#]|(\.(old|bak|orig|rej|diff|patch|core)|~|\/core)$/i =~ src

    bname = File.basename(src)
    name = case bname
      when 'rb_nibtool'
        bname
      else
        ruby_install_name.sub(/ruby/, bname)
    end

    shebang = ''
    body = ''
    open(src, "rb") do |f|
      shebang = f.gets
      body = f.read
    end
    shebang.sub!(/^\#!.*?ruby\b/) {"#!" + ruby_shebang}
    shebang.sub!(/\r$/, '')
    body.gsub!(/\r$/, '')

    cmd = File.join(bindir, name)
    cmd << ".#{$cmdtype}" if $cmdtype
    open_for_install(cmd, $script_mode) do
      if $cmdtype == "cmd"
        "#{<<EOH}#{shebang}#{body}"
@"%~dp0#{ruby_install_name}" -x "%~f0" %*
@exit /b %ERRORLEVEL%
EOH
      else
        shebang + body
      end
    end
  end
end

install?(:local, :data) do
  puts "installing data files"
  destination_dir = datadir.clone
  Config.expand(destination_dir)
  makedirs [destination_dir]
  install_recursive("data", destination_dir, :mode => $data_mode)
end

$install << :local << :ext if $install.empty?
$install.each do |inst|
  if !(procs = $install_procs[inst]) || procs.empty?
    next warn("unknown install target - #{inst}")
  end
  procs.each do |block|
    dir = Dir.pwd
    begin
      block.call
    ensure
      Dir.chdir(dir)
    end
  end
end

if RUBY_FRAMEWORK
  puts "installing framework"
  # Creating framework infrastructure.
  base = File.join(CONFIG["prefix"], '..')
  resources = File.join(base, 'Resources')
  mkdir_p resources, :mode => 0755
  install File.join('framework/Info.plist'), resources, :mode => 0644
  mkdir_p File.join(resources, 'English.lproj'), :mode => 0755
  install File.join('framework/InfoPlist.strings'),
    File.join(resources, 'English.lproj'), :mode => 0644
  rm_f File.join(base, '..', 'Current') if
    File.symlink?(with_destdir(File.join(base, '..', 'Current')))
  ln_sfh install_version.to_s, File.join(base, '..', 'Current')
  ln_sfh 'Versions/Current/Headers', File.join(base, '../../Headers')
  ln_sfh 'Versions/Current/MacRuby', File.join(base, '../../MacRuby')
  ln_sfh 'Versions/Current/Resources', File.join(base, '../../Resources')
  ln_sfh "usr/lib/#{CONFIG['LIBRUBY_SO']}", File.join(base, 'MacRuby')
  ln_sfh "usr/include/ruby-#{RUBY_VERSION}", File.join(base, 'Headers')
  ln_sfh "../#{CONFIG['arch']}/ruby/config.h",
    File.join(base, "usr/include/ruby-#{RUBY_VERSION}/ruby/config.h")
  # Installing executable links.
  dest_bin = File.join($sym_destdir, 'bin')
  mkdir_p dest_bin, :mode => 0755
  Dir.entries(with_destdir(CONFIG['bindir'])).each do |bin|
    next if bin[0] == '.'
    # Except rb_nibtool & llc!
    next if bin == 'rb_nibtool' or bin == 'llc'
    link = File.join("../../../", CONFIG['bindir'], bin)
    link.sub!(/#{install_version}/, 'Current')
    ln_sfh link, File.join(dest_bin, File.basename(bin))
  end
  # Installing man pages links.
  dest_man = File.join($sym_destdir, 'share', 'man')
  mkdir_p dest_man, :mode => 0755
  Dir.entries(with_destdir(CONFIG['mandir'])).each do |mandir|
    next if mandir[0] == '.'
    if File.stat(File.join(with_destdir(CONFIG['mandir']), mandir)).directory?
      mkdir_p File.join(dest_man, File.basename(mandir)), :mode => 0755
      Dir.entries(File.join(with_destdir(CONFIG['mandir']), mandir)).each do |man|
        next if man[0] == '.'
        link = File.join("../../../../../", CONFIG['mandir'], mandir, man)
        link.sub!(/#{install_version}/, 'Current')
        ln_sfh link, File.join(dest_man, File.basename(mandir),
	  File.basename(man))
      end
    else
      link = File.join("../../../../", CONFIG['mandir'], mandir)
      link.sub!(/#{install_version}/, 'Current')
      ln_sfh link, File.join(dest_man, File.basename(mandir))
    end
  end
end
