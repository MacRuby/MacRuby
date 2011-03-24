require 'test/unit/util/backtracefilter'

module Test
  module Unit
    class Omission
      include Util::BacktraceFilter
      attr_reader :test_name, :location, :message

      SINGLE_CHARACTER = 'O'
      LABEL = "Omission"

      # Creates a new Omission with the given location and
      # message.
      def initialize(test_name, location, message)
        @test_name = test_name
        @location = location
        @message = message
      end

      # Returns a single character representation of a omission.
      def single_character_display
        SINGLE_CHARACTER
      end

      def label
        LABEL
      end

      # Returns a brief version of the error description.
      def short_display
        "#{@test_name}: #{@message.split("\n")[0]}"
      end

      # Returns a verbose version of the error description.
      def long_display
        backtrace = filter_backtrace(location).join("\n")
        "#{label}: #{@message}\n#{@test_name}\n#{backtrace}"
      end

      # Overridden to return long_display.
      def to_s
        long_display
      end
    end

    class OmittedError < StandardError
    end


    module TestCaseOmissionSupport
      class << self
        def included(base)
          base.class_eval do
            include OmissionHandler
          end
        end
      end

      # Omit the test of part of the test.
      #
      # Example:
      #   def test_omission
      #     omit
      #     # Not reached here
      #   end
      #
      #   def test_omission_with_here
      #     omit do
      #       # Not ran here
      #     end
      #     # Reached here
      #   end
      def omit(message=nil, &block)
        message ||= "omitted."
        if block_given?
          omission = Omission.new(name, filter_backtrace(caller), message)
          add_omission(omission)
        else
          raise OmittedError.new(message)
        end
      end

      def omit_if(condition, *args, &block)
        omit(*args, &block) if condition
      end

      def omit_unless(condition, *args, &block)
        omit(*args, &block) unless condition
      end

      private
      def add_omission(omission)
        current_result.add_omission(omission)
      end
    end

    module OmissionHandler
      class << self
        def included(base)
          base.exception_handler(:handle_omitted_error)
        end
      end

      private
      def handle_omitted_error(exception)
        return false unless exception.is_a?(OmittedError)
        omission = Omission.new(name,
                                filter_backtrace(exception.backtrace),
                                exception.message)
        add_omission(omission)
        true
      end
    end

    module TestResultOmissionSupport
      attr_reader :omissions

      # Records a Test::Unit::Omission.
      def add_omission(omission)
        @omissions << omission
        notify_fault(omission)
        notify_changed
      end

      # Returns the number of omissions this TestResult has
      # recorded.
      def omission_count
        @omissions.size
      end

      private
      def initialize_containers
        super
        @omissions = []
        @summary_generators << :omission_summary
      end

      def omission_summary
        "#{omission_count} omissions"
      end
    end
  end
end
