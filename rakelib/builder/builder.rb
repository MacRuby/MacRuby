require File.expand_path('../options', __FILE__)

OBJS = %w{
  array bignum class compar complex enum enumerator error eval file load proc 
  gc hash inits io math numeric object pack parse prec dir process
  random range rational re onig/regcomp onig/regext onig/regposix onig/regenc
  onig/reggnu onig/regsyntax onig/regerror onig/regparse onig/regtrav
  onig/regexec onig/regposerr onig/regversion onig/enc/ascii onig/enc/unicode
  onig/enc/utf8 onig/enc/euc_jp onig/enc/sjis onig/enc/iso8859_1
  onig/enc/utf16_be onig/enc/utf16_le onig/enc/utf32_be onig/enc/utf32_le
  ruby signal sprintf st string struct time transcode util variable version
  thread id objc bs encoding main dln dmyext marshal gcd
  vm_eval prelude miniprelude gc-stub bridgesupport compiler dispatcher vm
  debugger MacRuby MacRubyDebuggerConnector NSDictionary
}

EXTENSIONS = %w{
  ripper digest etc readline libyaml fcntl socket zlib bigdecimal openssl json
}.sort

class Builder
  # Runs the given array of +commands+ in parallel. The amount of spawned
  # simultaneous jobs is determined by the `jobs' env variable. The default
  # value is 1.
  #
  # When the members of the +commands+ array are in turn arrays of strings,
  # then those commands will be executed in consecutive order.
  def self.parallel_execute(commands)
    commands = commands.dup

    Array.new(SIMULTANEOUS_JOBS) do |i|
      Thread.new do
        while c = commands.shift
          Array(c).each { |command| puts "[#{i}]"; sh(command) }
        end
      end
    end.each { |t| t.join }
  end

  attr_reader :objs, :cflags, :cxxflags
  attr_accessor :objc_cflags, :ldflags, :dldflags

  def initialize(objs)
    @objs = objs.dup
    @cflags = CFLAGS
    @cxxflags = CXXFLAGS
    @objc_cflags = OBJC_CFLAGS
    @ldflags = LDFLAGS
    @dldflags = DLDFLAGS
    @objs_cflags = OBJS_CFLAGS
    @obj_sources = {}
    @header_paths = {}
  end

  def build(objs=nil)
    objs ||= @objs
    objs.each do |obj| 
      if should_build?(obj) 
        s = obj_source(obj)
        cc, flags = 
          case File.extname(s)
            when '.c' then [CC, @cflags]
            when '.cpp' then [CXX, @cxxflags]
            when '.m' then [CC, @objc_cflags]
            when '.mm' then [CXX, @cxxflags + ' ' + @objc_cflags]
          end
        if f = @objs_cflags[obj]
          flags += " #{f}"
        end
        sh("#{cc} #{flags} -c #{s} -o #{obj}.o")
      end
    end
  end
 
  def link_executable(name, objs=nil, ldflags=nil)
    link(objs, ldflags, "-o #{name}", name)
  end

  def link_dylib(name, objs=nil, ldflags=nil)
    link(objs, ldflags, "#{@dldflags} -o #{name}", name)
  end

  def link_archive(name, objs=nil)
    objs ||= @objs
    if should_link?(name, objs)
      rm_f(name)
      sh("/usr/bin/ar rcu #{name} #{objs.map { |x| x + '.o' }.join(' ') }")
      sh("/usr/bin/ranlib #{name}")
    end
  end

  def clean
    @objs.map { |o| o + '.o' }.select { |o| File.exist?(o) }.each { |o| rm_f(o) }
  end
 
  private

  def link(objs, ldflags, args, name)
    objs ||= @objs
    ldflags ||= @ldflags
    if should_link?(name, objs)
      sh("#{CXX} #{@cflags} #{objs.map { |x| x + '.o' }.join(' ') } #{ldflags} #{args}")
    end
  end

  def should_build?(obj)
    if File.exist?(obj + '.o')
      src_time = File.mtime(obj_source(obj))
      obj_time = File.mtime(obj + '.o')
      src_time > obj_time \
        or dependencies[obj].any? { |f| File.mtime(f) > obj_time }
    else
      true
    end
  end

  def should_link?(bin, objs)
    if File.exist?(bin)
      mtime = File.mtime(bin)
      objs.any? { |o| File.mtime(o + '.o') > mtime }
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
    txt.scan(/#include\s+\"([^"]+)\"/).flatten.each do |header|
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
      @objs.each do |obj| 
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
      return [] unless File.exist?(makefile)
      [create_makefile_command, make_command(:clean), "rm -f #{makefile}"].compact
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

$builder = Builder.new(OBJS)
