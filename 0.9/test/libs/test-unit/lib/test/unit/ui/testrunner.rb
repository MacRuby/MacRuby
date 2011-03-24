require 'test/unit/ui/testrunnerutilities'

module Test
  module Unit
    module UI
      class TestRunner
        extend TestRunnerUtilities

        def initialize(suite, options={})
          if suite.respond_to?(:suite)
            @suite = suite.suite
          else
            @suite = suite
          end
          @options = options
        end
      end
    end
  end
end
