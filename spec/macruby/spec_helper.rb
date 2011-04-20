framework 'Cocoa'

SPEC_ROOT = File.dirname(__FILE__)
FIXTURES = File.join(SPEC_ROOT, "fixtures")
ROOT_DIR = File.expand_path("../../", SPEC_ROOT)

class FixtureCompiler
  def self.require!(fixture)
    new(fixture).require!
  end
  
  FRAMEWORKS = %w{ Foundation }
  ARCHS      = %w{ i386 x86_64 }
  OPTIONS    = %w{ -g -dynamiclib -fobjc-gc -Wl,-undefined,dynamic_lookup } << "-I#{ROOT_DIR}/include"
  GCC        = "/usr/bin/gcc"
  
  attr_reader :gcc, :frameworks, :archs, :options
  attr_reader :fixture, :bundle, :bridge_support
  
  def initialize(fixture)
    @fixture = File.join(FIXTURES, "#{fixture}.m")
    @bundle = File.join("/tmp", "#{fixture}.bundle")
    @bridge_support = File.join(FIXTURES, "#{fixture}.bridgesupport")
    
    @gcc, @frameworks, @archs, @options = [GCC, FRAMEWORKS, ARCHS, OPTIONS].map { |x| x.dup }
  end
  
  def require!
    compile!
    load!
  end
  
  private
  
  def needs_update?
    !File.exist?(bundle) or File.mtime(fixture) > File.mtime(bundle)
  end
  
  def compile!
    if needs_update?
      puts "[!] Compiling fixture `#{fixture}'"
      
      a = archs.map { |a| "-arch #{a}" }.join(' ')
      o = options.join(' ')
      f = frameworks.map { |f| "-framework #{f}" }.join(' ')
      
      `#{gcc} #{fixture} -o #{bundle} #{f} #{o} #{a}`
    end
  end
  
  def load!
    require bundle[0..-8]
    if File.exist? bridge_support
      load_bridge_support_file bridge_support
    end
  end
end

MACOSX_VERSION = `sw_vers -productVersion`.to_f
