require 'pathname'

require 'test/unit/testsuite'
require 'test/unit/collector'

module Test
  module Unit
    module Collector
      class Load
        include Collector

        attr_reader :patterns, :excludes, :base

        def initialize
          super
          @system_excludes = [/~\z/, /\A\.\#/]
          @system_directory_excludes = [/\A(?:CVS|\.svn)\z/]
          @patterns = [/\Atest[_\-].+\.rb\z/m]
          @excludes = []
          @base = nil
        end

        def base=(base)
          base = Pathname(base) unless base.nil?
          @base = base
        end

        def collect(*froms)
          add_load_path(@base) do
            froms = ["."] if froms.empty?
            test_suites = froms.collect do |from|
              test_suite = collect_recursive(from, find_test_cases)
              test_suite = nil if test_suite.tests.empty?
              test_suite
            end.compact

            if test_suites.size > 1
              test_suite = TestSuite.new("[#{froms.join(', ')}]")
              sort(test_suites).each do |sub_test_suite|
                test_suite << sub_test_suite
              end
              test_suite
            else
              test_suites.first
            end
          end
        end

        def find_test_cases(ignore=[])
          test_cases = []
          TestCase::DESCENDANTS.each do |test_case|
            test_cases << test_case unless ignore.include?(test_case)
          end
          ignore.concat(test_cases)
          test_cases
        end

        private
        def collect_recursive(name, already_gathered)
          sub_test_suites = []

          path = resolve_path(name)
          if path.directory?
            directories, files = path.children.partition do |child|
              child.directory?
            end

            files.each do |child|
              next if excluded_file?(child.basename.to_s)
              collect_file(child, sub_test_suites, already_gathered)
            end

            directories.each do |child|
              next if excluded_directory?(child.basename.to_s)
              sub_test_suite = collect_recursive(child, already_gathered)
              sub_test_suites << sub_test_suite unless sub_test_suite.empty?
            end
          else
            unless excluded_file?(path.basename.to_s)
              collect_file(path, sub_test_suites, already_gathered)
            end
          end

          test_suite = TestSuite.new(path.basename.to_s)
          sort(sub_test_suites).each do |sub_test_suite|
            test_suite << sub_test_suite
          end
          test_suite
        end

        def collect_file(path, test_suites, already_gathered)
          add_load_path(path.expand_path.dirname) do
            require(path.to_s)
            find_test_cases(already_gathered).each do |test_case|
              add_suite(test_suites, test_case.suite)
            end
          end
        end

        def resolve_path(path)
          if @base
            @base + path
          else
            Pathname(path)
          end
        end

        def add_load_path(path)
          $LOAD_PATH.push(path.to_s) if path
          yield
        ensure
          $LOAD_PATH.delete_at($LOAD_PATH.rindex(path.to_s)) if path
        end

        def excluded_directory?(base)
          @system_directory_excludes.any? {|pattern| pattern =~ base}
        end

        def excluded_file?(base)
          return true if @system_excludes.any? {|pattern| pattern =~ base}

          patterns = @patterns || []
          unless patterns.empty?
            return true unless patterns.any? {|pattern| pattern =~ base}
          end

          excludes = @excludes || []
          unless excludes.empty?
            return true if excludes.any? {|pattern| pattern =~ base}
          end

          false
        end
      end
    end
  end
end
