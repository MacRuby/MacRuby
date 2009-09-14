#--
#
# Author:: Nathaniel Talbott.
# Copyright:: Copyright (c) 2000-2002 Nathaniel Talbott. All rights reserved.
# License:: Ruby license.

require 'test/unit'
require 'test/unit/util/observable'
require 'test/unit/testresult'

module Test
  module Unit
    module UI

      # Provides an interface to write any given UI against,
      # hopefully making it easy to write new UIs.
      class TestRunnerMediator
        RESET = name + "::RESET"
        STARTED = name + "::STARTED"
        FINISHED = name + "::FINISHED"
        
        include Util::Observable
        
        # Creates a new TestRunnerMediator initialized to run
        # the passed suite.
        def initialize(suite)
          @suite = suite
        end

        # Runs the suite the TestRunnerMediator was created
        # with.
        def run_suite
          Unit.run = true

          result = create_result
          result_listener = result.add_listener(TestResult::CHANGED) do |*args|
            notify_listeners(TestResult::CHANGED, *args)
          end
          fault_listener = result.add_listener(TestResult::FAULT) do |*args|
            notify_listeners(TestResult::FAULT, *args)
          end

          start_time = Time.now
          begin
            notify_listeners(RESET, @suite.size)
            notify_listeners(STARTED, result)

            @suite.run(result) do |channel, value|
              notify_listeners(channel, value)
            end
          ensure
            elapsed_time = Time.now - start_time
            result.remove_listener(TestResult::FAULT, fault_listener)
            result.remove_listener(TestResult::CHANGED, result_listener)
            notify_listeners(FINISHED, elapsed_time)
          end

          result
        end

        private
        # A factory method to create the result the mediator
        # should run with. Can be overridden by subclasses if
        # one wants to use a different result.
        def create_result
          TestResult.new
        end

        def measure_time
          begin_time = Time.now
          yield
          Time.now - begin_time
        end
      end
    end
  end
end
