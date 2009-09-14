require 'test/unit/collector'

module Test
  module Unit
    module Collector
      class Descendant
        include Collector

        NAME = 'collected from the subclasses of TestCase'

        def collect(name=NAME)
          suite = TestSuite.new(name)
          sub_suites = []
          TestCase::DESCENDANTS.each do |descendant_test_case|
            add_suite(sub_suites, descendant_test_case.suite)
          end
          sort(sub_suites).each {|s| suite << s}
          suite
        end
      end
    end
  end
end
