# Define a task library for compilation with MacRuby.

require 'rake'
require 'rake/tasklib'
require 'rbconfig'

# it could already be loaded by rubygems or others
unless MacRuby.const_defined?(:Compiler)
  load File.join(RbConfig::CONFIG['bindir'], 'macrubyc')
end

module Rake

  # Create a task that compiles your ruby source files for MacRuby.
  #
  # Example: What To Add To Your Rakefile
  #
  #   require 'rake/compiletask'
  #   Rake::CompileTask.new(:rbo)
  #
  # Example: Compiling On Demand
  #
  #   rake rbo
  #
  class CompileTask < TaskLib

    # Name of test task. (default is :compile)
    attr_reader :name

    # Whether to list each file that is being compiled or not
    # (defaults is true)
    attr_accessor :verbose

    # List of directories where to look for .rb files that should be compiled
    # (default is 'lib')
    attr_accessor :libs

    # Explicitly define extra files that should be compiled.
    # You must set this attribute to be an array of file names (a FileList
    # is acceptable).
    #
    # Note that this list is in addition to files found in directories
    # listed in +libs+
    attr_accessor :files

    # Create a MacRuby compilation task.
    def initialize(name=:compile)
      @name = name
      @libs = ['lib']
      @files = []
      @verbose = true
      yield self if block_given?
      define
    end

    # Create the tasks defined by this task lib.
    def define
      desc 'Compile ruby files' + (name == :compile ? '' : " for #{name}")
      task name do
        start_time = Time.now
        number_of_files = 0

        full_file_list.each do |source|
          compiled_name = "#{source}o"

          if File.exists?(compiled_name) && (File.mtime(compiled_name) > File.mtime(source))
            next
          end

          if verbose
            $stdout.puts compiled_name
            number_of_files += 1
          end

          MacRuby::Compiler.compile_file(source)
        end

        if verbose
          compile_time = Time.now - start_time
          $stdout.puts "Finished compile in %.6fs, %.6s files/s" %
            [compile_time, number_of_files / compile_time]
        end
      end

      desc "Remove files compiled with :#{name}"
      task "clobber_#{name}" do
        full_file_list.each do |file|
          path = "#{file}o"
          next unless File.exists?(path)
          $stdout.puts "rm #{path}" if verbose
          rm path
        end
      end
      task :clobber => ["clobber_#{name}"]

      self
    end

    def full_file_list
      (libs.inject([]) do |files, dir|
        files << Dir.glob(File.join("#{dir}", '**', '*.rb'))
      end + files).flatten.uniq
    end

  end
end
