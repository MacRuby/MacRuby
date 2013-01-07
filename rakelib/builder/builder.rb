# Make sure File.read will work as expected on any Ruby.
Encoding.default_external = "UTF-8" if defined?(Encoding)

require File.expand_path('../options', __FILE__)

EXTENSIONS = %w{
  ripper digest etc readline libyaml fcntl socket zlib bigdecimal openssl json
  nkf iconv io gdbm
}.sort

class Builder
  if defined?(Rake::DSL)
    extend Rake::DSL
    include Rake::DSL
  end

  # Runs the given array of +commands+ in parallel. The amount of spawned
  # simultaneous jobs is determined by the `jobs' env variable. The default
  # value is 1.
  #
  # When the members of the +commands+ array are in turn arrays of strings,
  # then those commands will be executed in consecutive order.
  def self.parallel_execute(commands)
    commands = commands.dup

    Array.new(SIMULTANEOUS_JOBS) do
      Thread.new do
        while c = commands.shift
          Array(c).each { |command| sh(command) }
        end
      end
    end.each { |t| t.join }
  end

  [:objs, :archs, :cflags, :cxxflags, :objc_cflags, :ldflags, :objsdir,
   :objs_cflags, :dldflags].each do |sym|
    define_method(sym) { @config.send(sym) }
  end 

  def initialize(config)
    self.config = config
  end

  def config=(c)
    if @config != c
      @config = c
      @obj_sources = {}
      @header_paths = {}
      FileUtils.mkdir_p(@config.objsdir)
    end
  end

  def build(objs=nil)
    objs ||= @config.objs
    commands = []
    objs.each do |obj| 
      if should_build?(obj) 
        s = obj_source(obj)
        cc, flags = 
          case File.extname(s)
            when '.c' then [@config.CC, @config.cflags]
            when '.cpp' then [@config.CXX, @config.cxxflags]
            when '.m' then [@config.CC, @config.objc_cflags]
            when '.mm' then [@config.CXX, @config.cxxflags + ' ' + @config.objc_cflags]
          end
        if f = @config.objs_cflags[obj]
          flags += " #{f}"
        end
        commands << "#{cc} #{flags} -c #{s} -o #{obj_path(obj)}"
      end
    end
    self.class.parallel_execute(commands)
  end
 
  def link_executable(name, objs=nil, ldflags=nil)
    link(objs, ldflags, "-o #{name}", name)
  end

  def link_dylib(name, objs=nil, ldflags=nil)
    link(objs, ldflags, "#{@config.dldflags} -o #{name}", name)
  end

  def link_archive(name, objs=nil)
    objs ||= @config.objs
    if should_link?(name, objs)
      rm_f(name)
      sh("/usr/bin/ar rcu #{name} #{objs.map { |x| obj_path(x) }.join(' ') }")
      sh("/usr/bin/ranlib #{name}")
    end
  end
 
  private

  def obj_path(o)
    raise unless @config.objsdir
    File.join(@config.objsdir, o + '.o')
  end

  def link(objs, ldflags, args, name)
    objs ||= @config.objs
    ldflags ||= @config.ldflags
    if should_link?(name, objs)
      sh("#{CXX} #{@config.cflags} #{objs.map { |x| obj_path(x) }.join(' ') } #{ldflags} #{args}")
    end
  end

  def should_build?(obj)
    if File.exist?(obj_path(obj))
      src_time = File.mtime(obj_source(obj))
      obj_time = File.mtime(obj_path(obj))
      src_time > obj_time \
        or dependencies[obj].any? { |f| File.mtime(f) > obj_time }
    else
      true
    end
  end

  def should_link?(bin, objs)
    if File.exist?(bin)
      mtime = File.mtime(bin)
      objs.any? { |o| File.mtime(obj_path(o)) > mtime }
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
      s = ['.c', '.cpp', '.m', '.mm'].map { |e| obj + e }.find { |p| File.exist?(p) }
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
    txt.scan(/#\s*include\s+\"([^"]+)\"/).flatten.each do |header|
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
      @config.objs.each do |obj| 
        ary = []
        locate_headers(ary, obj_source(obj))
        @obj_dependencies[obj] = ary.uniq
      end
    end
    @obj_dependencies
  end
  
  class Ext
    def self.extension_dirs
      EXTENSIONS.map do |name|
        Dir.glob(File.join('ext', name, '**/extconf.rb'))
      end.flatten.map { |f| File.dirname(f) }
    end
    
    def self.build
      commands = extension_dirs.map { |dir| new(dir).build_commands }
      Builder.parallel_execute(commands)
    end
    
    def self.install
      extension_dirs.each do |dir|
        sh new(dir).install_command
      end
    end
    
    extend Rake::DSL if defined?(Rake::DSL)
    def self.clean
      extension_dirs.each do |dir|
        new(dir).clean_commands.each { |cmd| sh(cmd) }
      end
    end
    
    attr_reader :dir
    
    def initialize(dir)
      @dir = dir
    end
    
    def srcdir
      @srcdir ||= File.join(dir.split(File::SEPARATOR).map { |x| '..' })
    end
    
    def makefile
      @makefile ||= File.join(@dir, 'Makefile')
    end
    
    def extconf
      File.join(@dir, 'extconf.rb')
    end
    
    def create_makefile_command
      if !File.exist?(makefile) or File.mtime(extconf) > File.mtime(makefile)
        "cd #{dir} && #{srcdir}/miniruby -I#{srcdir} -I#{srcdir}/lib -r rbconfig -e \"RbConfig::CONFIG['libdir'] = '#{srcdir}'; require './extconf.rb'\""
      end
    end
    
    def build_commands
      [create_makefile_command, make_command(:all)].compact
    end
    
    def clean_commands
      if File.exist?(makefile)
        [make_command(:clean), "rm -f #{makefile}"].compact
      else
        []
      end
    end
    
    def install_command
      make_command(:install)
    end
    
    private
    
    # Possible targets are:
    # * all
    # * install
    # * clean
    def make_command(target)
      cmd = "cd #{dir} && /usr/bin/make top_srcdir=#{srcdir} ruby=\"#{srcdir}/miniruby -I#{srcdir} -I#{srcdir}/lib\" extout=#{srcdir}/.ext hdrdir=#{srcdir}/include arch_hdrdir=#{srcdir}/include"
      cmd << (target == :all ? " libdir=#{srcdir}" : " #{target}")
      cmd
    end
  end
end

$builder = Builder.new(FULL_CONFIG)
