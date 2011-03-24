# Author:: Nathaniel Talbott.
# Copyright:: Copyright (c) 2000-2002 Nathaniel Talbott. All rights reserved.
# License:: Ruby license.

require 'test/unit'

module Test
  module Unit
    class TestTestCase < TestCase
      def test_creation
        tc = Class.new(TestCase) do
          def test_with_arguments(arg1, arg2)
          end
        end
      
        caught = true
        catch(:invalid_test) do
          tc.new(:test_with_arguments)
          caught = false
        end
        check("Should have caught an invalid test when there are arguments", caught)
        
        caught = true
        catch(:invalid_test) do
          tc.new(:non_existent_test)
          caught = false
        end
        check("Should have caught an invalid test when the method does not exist", caught)
      end
      
      def setup
        @tc_failure_error = Class.new(TestCase) do
          def test_failure
            assert_block("failure") { false }
          end
          def test_error
            1 / 0
          end
          def test_nested_failure
            nested
          end
          def nested
            assert_block("nested"){false}
          end
          def return_passed?
            return passed?
          end
        end

        def @tc_failure_error.name
          "TC_FailureError"
        end
      end
 
      def test_add_failed_assertion
        test_case = @tc_failure_error.new(:test_failure)
        check("passed? should start out true", test_case.return_passed?)
        result = TestResult.new
        called = false
        result.add_listener(TestResult::FAULT) {
          | fault |
          check("Should have a Failure", fault.instance_of?(Failure))
          check("The Failure should have the correct message", "failure" == fault.message)
          check("The Failure should have the correct test_name (was <#{fault.test_name}>)", fault.test_name == "test_failure(TC_FailureError)")
          r = /\A.*#{Regexp.escape(File.basename(__FILE__))}:\d+:in `test_failure'\Z/

          location = fault.location
          check("The location should be an array", location.kind_of?(Array))
          check("The location should have two lines (was: <#{location.inspect}>)", location.size == 2)
          check("The Failure should have the correct location (was <#{location[0].inspect}>, expected <#{r.inspect}>)", r =~ location[0])
          called = true
        }
        progress = []
        test_case.run(result) { |*arguments| progress << arguments }
        check("The failure should have triggered the listener", called)
        check("The failure should have set passed?", !test_case.return_passed?)
        check("The progress block should have been updated correctly", [[TestCase::STARTED, test_case.name], [TestCase::FINISHED, test_case.name]] == progress)
      end

      def test_add_failure_nested
        test_case = @tc_failure_error.new(:test_nested_failure)
        check("passed? should start out true", test_case.return_passed?)

        result = TestResult.new
        called = false
        result.add_listener(TestResult::FAULT) {
          | fault |
          check("Should have a Failure", fault.instance_of?(Failure))
          check("The Failure should have the correct message", "nested" == fault.message)
          check("The Failure should have the correct test_name (was <#{fault.test_name}>)", fault.test_name == "test_nested_failure(TC_FailureError)")
          r = 

          location = fault.location
          check("The location should be an array", location.kind_of?(Array))
          check("The location should have the correct number of lines (was: <#{location.inspect}>)", location.size == 3)
          check("The Failure should have the correct location (was <#{location[0].inspect}>)", /\A.*#{Regexp.escape(File.basename(__FILE__))}:\d+:in `nested'\Z/ =~ location[0])
          check("The Failure should have the correct location (was <#{location[1].inspect}>)", /\A.*#{Regexp.escape(File.basename(__FILE__))}:\d+:in `test_nested_failure'\Z/ =~ location[1])
          called = true
        }
        test_case.run(result){}
        check("The failure should have triggered the listener", called)
      end
      
      def test_add_error
        test_case = @tc_failure_error.new(:test_error)
        check("passed? should start out true", test_case.return_passed?)
        result = TestResult.new
        called = false
        result.add_listener(TestResult::FAULT) {
          | fault |
          check("Should have a TestError", fault.instance_of?(Error))
          check("The Error should have the correct message", "ZeroDivisionError: divided by 0" == fault.message)
          check("The Error should have the correct test_name", "test_error(TC_FailureError)" == fault.test_name)
          check("The Error should have the correct exception", fault.exception.instance_of?(ZeroDivisionError))
          called = true
        }
        test_case.run(result) {}
        check("The error should have triggered the listener", called)
        check("The error should have set passed?", !test_case.return_passed?)
      end

      def test_no_tests      
        suite = TestCase.suite
        check("Should have a test suite", suite.instance_of?(TestSuite))
        check("Should have one test", suite.size == 1)
        check("Should have the default test", suite.tests.first.name == "default_test(Test::Unit::TestCase)")
        
        result = TestResult.new
        suite.run(result) {}
        check("Should have had one test run", result.run_count == 1)
        check("Should have had one test failure", result.failure_count == 1)
        check("Should have had no errors", result.error_count == 0)
      end

      def test_suite
        tc = Class.new(TestCase) do
          def test_succeed
            assert_block {true}
          end
          def test_fail
            assert_block {false}
          end
          def test_error
            1/0
          end
          def dont_run
            assert_block {true}
          end
          def test_dont_run(argument)
            assert_block {true}
          end
          def test
            assert_block {true}
          end
        end
      
        suite = tc.suite
        check("Should have a test suite", suite.instance_of?(TestSuite))
        check("Should have three tests", suite.size == 3)
  
        result = TestResult.new
        suite.run(result) {}
        check("Should have had three test runs", result.run_count == 3)
        check("Should have had one test failure", result.failure_count == 1)
        check("Should have had one test error", result.error_count == 1)
      end
      
     
      def test_setup_teardown
        tc = Class.new(TestCase) do
          attr_reader(:setup_called, :teardown_called)
          def initialize(test)
            super(test)
            @setup_called = false
            @teardown_called = false
          end
          def setup
            @setup_called = true
          end
          def teardown
            @teardown_called = true
          end
          def test_succeed
            assert_block {true}
          end
          def test_fail
            assert_block {false}
          end
          def test_error
            raise "Error!"
          end
        end
        result = TestResult.new
  
        test = tc.new(:test_succeed)
        test.run(result) {}
        check("Should have called setup the correct number of times", test.setup_called)
        check("Should have called teardown the correct number of times", test.teardown_called)
  
        test = tc.new(:test_fail)
        test.run(result) {}
        check("Should have called setup the correct number of times", test.setup_called)
        check("Should have called teardown the correct number of times", test.teardown_called)
  
        test = tc.new(:test_error)
        test.run(result) {}
        check("Should have called setup the correct number of times", test.setup_called)
        check("Should have called teardown the correct number of times", test.teardown_called)
  
        check("Should have had two test runs", result.run_count == 3)
        check("Should have had a test failure", result.failure_count == 1)
        check("Should have had a test error", result.error_count == 1)
      end
      
      def test_assertion_failed_not_called
        tc = Class.new(TestCase) do
          def test_thing
            raise AssertionFailedError.new
          end
        end
        
        suite = tc.suite
        check("Should have one test", suite.size == 1)
        result = TestResult.new
        suite.run(result) {}
        check("Should have had one test run", result.run_count == 1)
        check("Should have had one assertion failure", result.failure_count == 1)
        check("Should not have any assertion errors but had #{result.error_count}", result.error_count == 0)
      end
      
      def test_equality
        tc1 = Class.new(TestCase) do
          def test_1
          end
          def test_2
          end
        end
        
        tc2 = Class.new(TestCase) do
          def test_1
          end
        end
      
        test1 = tc1.new('test_1')
        test2 = tc1.new('test_1')
        check("Should be equal", test1 == test2)
        check("Should be equal", test2 == test1)
        
        test1 = tc1.new('test_2')
        check("Should not be equal", test1 != test2)
        check("Should not be equal", test2 != test1)
        
        test2 = tc1.new('test_2')
        check("Should be equal", test1 == test2)
        check("Should be equal", test2 == test1)
        
        test1 = tc1.new('test_1')
        test2 = tc2.new('test_1')
        check("Should not be equal", test1 != test2)
        check("Should not be equal", test2 != test1)

        
        check("Should not be equal", test1 != Object.new)
        check("Should not be equal", Object.new != test1)
      end

      def test_re_raise_exception
        test_case = Class.new(TestCase) do
          def test_raise_interrupt
            raise Interrupt, "from test"
          end
        end

        test = test_case.new("test_raise_interrupt")
        begin
          test.run(TestResult.new) {}
          check("Should not be reached", false)
        rescue Exception
          check("Interrupt exception should be re-raised", $!.class == Interrupt)
        end
      end

      def test_startup_shutdown
        called = []
        test_case = Class.new(TestCase) do
          @@called = called
          class << self
            def startup
              @@called << :startup
            end

            def shutdown
              @@called << :shutdown
            end
          end

          def setup
            @@called << :setup
          end

          def teardown
            @@called << :teardown
          end

          def test1
          end

          def test2
          end
        end

        test_suite = test_case.suite
        test_suite.run(TestResult.new) {}
        check("startup/shutdown should be called once per test case" +
              ": #{called.inspect}",
              called == [:startup,
                         :setup, :teardown,
                         :setup, :teardown,
                         :shutdown])
      end

      def test_error_on_startup
        test_case = Class.new(TestCase) do
          class << self
            def startup
              raise "from startup"
            end
          end

          def test_nothing
          end
        end

        test_suite = test_case.suite
        result = TestResult.new
        test_suite.run(result) {}
        check("Should record an error on startup: #{result}",
              result.error_count == 1)
      end

      def test_pass_through_error_on_startup
        test_case = Class.new(TestCase) do
          class << self
            def startup
              raise Interrupt, "from startup"
            end
          end

          def test_nothing
          end
        end

        test_suite = test_case.suite
        begin
          test_suite.run(TestResult.new) {}
          check("Should not be reached", false)
        rescue Exception
          check("Interrupt should be passed through: #{$!}",
                Interrupt === $!)
        end
      end

      def test_error_on_shutdown
        test_case = Class.new(TestCase) do
          class << self
            def shutdown
              raise "from shutdown"
            end
          end

          def test_nothing
          end
        end

        test_suite = test_case.suite
        result = TestResult.new
        test_suite.run(result) {}
        check("Should record an error on shutdown: #{result}",
              result.error_count == 1)
      end

      def test_pass_through_error_on_shutdown
        test_case = Class.new(TestCase) do
          class << self
            def shutdown
              raise Interrupt, "from shutdown"
            end
          end

          def test_nothing
          end
        end

        test_suite = test_case.suite
        begin
          test_suite.run(TestResult.new) {}
          check("Should not be reached", false)
        rescue Exception
          check("Interrupt should be passed through: #{$!}",
                Interrupt === $!)
        end
      end

      def test_interrupted
        test_case = Class.new(TestCase) do
          def test_fail
            flunk
          end

          def test_nothing
          end
        end

        failed_test = test_case.new(:test_fail)
        failed_test.run(TestResult.new) {}
        check("Should be interrupted", failed_test.interrupted?)

        success_test = test_case.new(:test_nothing)
        success_test.run(TestResult.new) {}
        check("Should not be interrupted", !success_test.interrupted?)
      end

      def test_inherited_test_should_be_ignored
        test_case = Class.new(TestCase) do
          def test_nothing
          end
        end

        sub_test_case = Class.new(test_case) do
          def test_fail
            flunk
          end
        end

        assert_nothing_thrown do
          test_case.new("test_nothing")
        end

        assert_nothing_thrown do
          sub_test_case.new("test_fail")
        end

        assert_throw(:invalid_test) do
          sub_test_case.new("test_nothing")
        end
      end

      def test_mixin_test_should_not_be_ignored
        test_module = Module.new do
          def test_nothing
          end
        end

        test_case = Class.new(Test::Unit::TestCase) do
          include test_module

          def test_fail
            flunk
          end
        end

        assert_nothing_thrown do
          test_case.new("test_nothing")
        end

        assert_nothing_thrown do
          test_case.new("test_fail")
        end
      end

      def test_defined_order
        keep_test_order do
          test_case = Class.new(Test::Unit::TestCase) do
            def test_z
            end

            def test_1
            end

            def test_a
            end
          end

          assert_equal(["test_1", "test_a", "test_z"],
                       test_case.suite.tests.collect {|test| test.method_name})

          test_case.test_order = :defined
          assert_equal(["test_z", "test_1", "test_a"],
                       test_case.suite.tests.collect {|test| test.method_name})
        end
      end

      private
      def check(message, passed)
        add_assertion
        raise AssertionFailedError.new(message) unless passed
      end

      def keep_test_order
        order = TestCase.test_order
        yield
      ensure
        TestCase.test_order = order
      end
    end
  end
end
