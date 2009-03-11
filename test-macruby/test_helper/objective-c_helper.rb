require File.expand_path('../temp_dir_helper', __FILE__)
require 'dl'

# Eloy: From Kari, will probably move to Rucola, so need to update.
class ObjectiveC
  class CompileError < ::StandardError; end
  
  include TempDirHelper
  
  # Returns a new ObjectiveC compile instance with the +source_file+ and the
  # +frameworks+ needed to compile this +source_file+. Foundation is added to
  # the +frameworks+ by default.
  #
  #   objc = ObjectiveC.new('/path/to/source_file.m', 'WebKit')
  #   objc.compile!
  #   objc.require!
  def initialize(source_file, *frameworks)
    @path, @frameworks = source_file, frameworks
    @frameworks.unshift 'Foundation'
  end
  
  # Compiles the +source_file+. See new.
  # Raises a ObjectiveC::CompileError if compilation failed.
  #
  # TODO: Check the arch build flags etc.
  def compile!
    ensure_dir!(output_dir)
    
    frameworks = @frameworks.map { |f| "-framework #{f}" }.join(' ')
    command = "gcc -o #{ bundle_path } -arch x86_64 -fobjc-gc -flat_namespace -undefined suppress -bundle #{ frameworks } #{ includes } #{ @path }"
    unless system(command)
      raise CompileError, "Unable to compile file `#{ File.basename(@path) }'."
    end
  end
  
  # Loads the compiled bundle with <tt>DL.dlopen</tt>.
  def require!
    DL.dlopen(bundle_path)
  end
  
  private
  
  def includes
    "-I#{ File.dirname(@path) }"
  end
  
  def output_dir
    temp_dir 'bundles'
  end
  
  def bundle_path
    File.join output_dir, "#{ File.basename(@path, '.m') }.bundle"
  end
end