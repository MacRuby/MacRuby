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
