#--
# Author:: Nathaniel Talbott.
# Copyright:: Copyright (c) 2000-2002 Nathaniel Talbott. All rights reserved.
# License:: Ruby license.

require 'test/unit/util/observable'
require 'test/unit/failure'
require 'test/unit/error'
require 'test/unit/omission'
require 'test/unit/pending'
require 'test/unit/notification'

module Test
  module Unit
    module NullResultContainerInitializer
      private
      def initialize_containers
      end
    end

    # Collects Test::Unit::Failure and Test::Unit::Error so that
    # they can be displayed to the user. To this end, observers
    # can be added to it, allowing the dynamic updating of, say, a
    # UI.
    class TestResult
      include Util::Observable
      include NullResultContainerInitializer
      include TestResultFailureSupport
      include TestResultErrorSupport
      include TestResultPendingSupport
      include TestResultOmissionSupport
      include TestResultNotificationSupport

      CHANGED = "CHANGED"
      FAULT = "FAULT"

      attr_reader :run_count, :assertion_count, :faults

      # Constructs a new, empty TestResult.
      def initialize
        @run_count, @assertion_count = 0, 0
        @summary_generators = []
        @problem_checkers = []
        @faults = []
        initialize_containers
      end

      # Records a test run.
      def add_run
        @run_count += 1
        notify_changed
      end

      # Records an individual assertion.
      def add_assertion
        @assertion_count += 1
        notify_changed
      end

      # Returns a string contain the recorded runs, assertions,
      # failures and errors in this TestResult.
      def summary
        ["#{run_count} tests",
         "#{assertion_count} assertions",
         *@summary_generators.collect {|generator| send(generator)}].join(", ")
      end

      def to_s
        summary
      end

      # Returns whether or not this TestResult represents
      # successful completion.
      def passed?
        @problem_checkers.all? {|checker| not send(checker)}
      end

      private
      def notify_changed
        notify_listeners(CHANGED, self)
      end

      def notify_fault(fault)
        @faults << fault
        notify_listeners(FAULT, fault)
      end
    end
  end
end
