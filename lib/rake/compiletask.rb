# Define a task library for compilation with MacRuby.

require 'rake'
require 'rake/tasklib'
require 'rbconfig'
load File.join(RbConfig::CONFIG['bindir'], 'macrubyc')

module Rake

  # Create a task that runs a set of tests.
  #
  # Example:
  #
  #   Rake::CompileTask.new do |t|
  #     t.files = FileList['lib/**/*.rb']
  #     t.verbose = true
  #   end
  #
  # Example:
  #
  #   rake compile                  # regular compilation
  #
  class CompileTask < TaskLib

    # Name of test task. (default is :compile)
    attr_accessor :name

    # Whether to list each file that is compiled or not (defaults is false)
    attr_accessor :verbose

    # Explicitly define the list of test files to be compiled.
    # +list+ is expected to be an array of file names (a
    # FileList is acceptable).
    def files=(list)
      @files = list
    end

    # Create a testing task.
    def initialize(name=:compile)
      @name = name
      @files = []
      @verbose = false
      yield self if block_given?
      define
    end

    # Create the tasks defined by this task lib.
    def define
      desc "Compile files" + (@name==:compile ? "" : " for #{@name}")
      task @name do
        start_time = Time.now
        number_of_files = 0

        @files.each do |source|
          compiled_name = "#{source}o"

          if File.exists?(compiled_name) && (File.mtime(compiled_name) > File.mtime(source))
            next
          end

          if @verbose
            $stdout.puts compiled_name
            number_of_files += 1
          end

          MacRuby::Compiler.new(output: compiled_name,
                                bundle: true,
                                 files: [source]
                                ).run
        end

        if @verbose
          compile_time = Time.now - start_time
          $stdout.puts "Finished compile in %.6fs, %.6s files/s" %
            [compile_time, number_of_files / compile_time]
        end
      end
      self
    end

  end
end
