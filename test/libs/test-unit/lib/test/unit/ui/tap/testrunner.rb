#--
#
# Author:: Kouhei Sutou.
# Copyright:: Copyright (c) 2009 Kouhei Sutou <kou@clear-code.com>.
# License:: Ruby license.

require 'test/unit/ui/testrunner'
require 'test/unit/ui/testrunnermediator'

module Test
  module Unit
    module UI
      module Tap

        # Runs a Test::Unit::TestSuite and outputs result
        # as TAP format.
        class TestRunner < UI::TestRunner
          def initialize(suite, options={})
            super
            @output = @options[:output] || STDOUT
            @n_tests = 0
            @already_outputted = false
          end

          # Begins the test run.
          def start
            setup_mediator
            result = start_mediator
            def result.passed?
              true # for prove commend :<
            end
            result
          end

          private
          def setup_mediator
            @mediator = TestRunnerMediator.new(@suite)
            attach_to_mediator
          end

          def attach_to_mediator
            @mediator.add_listener(TestResult::FAULT, &method(:add_fault))
            @mediator.add_listener(TestRunnerMediator::STARTED, &method(:started))
            @mediator.add_listener(TestRunnerMediator::FINISHED, &method(:finished))
            @mediator.add_listener(TestCase::STARTED, &method(:test_started))
            @mediator.add_listener(TestCase::FINISHED, &method(:test_finished))
          end

          def start_mediator
            @mediator.run_suite
          end

          def add_fault(fault)
            puts("not ok #{@n_tests} - #{fault.short_display}")
            fault.long_display.each_line do |line|
              puts("# #{line}")
            end
            @already_outputted = true
          end

          def started(result)
            @result = result
            puts("1..#{@suite.size}")
          end

          def finished(elapsed_time)
            puts("# Finished in #{elapsed_time} seconds.")
            @result.to_s.each_line do |line|
              puts("# #{line}")
            end
          end

          def test_started(name)
            @n_tests += 1
          end

          def test_finished(name)
            unless @already_outputted
              puts("ok #{@n_tests} - #{name}")
            end
            @already_outputted = false
          end

          def puts(*args)
            @output.puts(*args)
            @output.flush
          end
        end
      end
    end
  end
end
