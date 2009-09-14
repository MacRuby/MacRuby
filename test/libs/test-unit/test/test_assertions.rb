# Author:: Nathaniel Talbott.
# Copyright:: Copyright (c) 2000-2002 Nathaniel Talbott. All rights reserved.
#             Copyright (c) 2009 Kouhei Sutou.
# License:: Ruby license.

require 'test/unit'

module Test
  module Unit
    class TC_Assertions < TestCase
      backtrace_pre  = "---Backtrace---"
      backtrace_post = "---------------"
      BACKTRACE_RE = /#{backtrace_pre}\n.+\n#{backtrace_post}/m

      def check(value, message="")
        add_assertion
        raise AssertionFailedError.new(message) unless value
      end

      def check_assertions(expect_fail, expected_message="",
                           return_value_expected=false)
        @actual_assertion_count = 0
        failed = true
        actual_message = nil
        @catch_assertions = true
        return_value = nil
        begin
          return_value = yield
          failed = false
        rescue AssertionFailedError => error
          actual_message = error.message
        end
        @catch_assertions = false

        if expect_fail
          message = "Should have failed, but didn't"
        else
          message = "Should not have failed, but did with message\n" +
            "<#{actual_message}>"
        end
        check(expect_fail == failed, message)

        message = "Should have made one assertion but made\n" +
          "<#{@actual_assertion_count}>"
        check(1 == @actual_assertion_count, message)

        if expect_fail
          case expected_message
          when String
            check(actual_message == expected_message,
                  "Should have the correct message.\n" +
                  "<#{expected_message.inspect}> expected but was\n" +
                  "<#{actual_message.inspect}>")
          when Regexp
            check(actual_message =~ expected_message,
                  "The message should match correctly.\n" +
                  "</#{expected_message.source}/> expected to match\n" +
                  "<#{actual_message.inspect}>")
          else
            check(false,
                  "Incorrect expected message type in assert_nothing_failed")
          end
        else
          if return_value_expected
            check(!return_value.nil?, "Should return a value")
          else
            check(return_value.nil?,
                  "Should not return a value but returned <#{return_value}>")
          end
        end

        return_value
      end

      def check_nothing_fails(return_value_expected=false, &proc)
        check_assertions(false, "", return_value_expected, &proc)
      end

      def check_fails(expected_message="", &proc)
        check_assertions(true, expected_message, &proc)
      end

      def inspect_tag(tag)
        begin
          throw tag
        rescue NameError
          tag.to_s.inspect
        rescue ArgumentError
          tag.inspect
        end
      end

      def test_assert_block
        check_nothing_fails {
          assert_block {true}
        }
        check_nothing_fails {
          assert_block("successful assert_block") {true}
        }
        check_nothing_fails {
          assert_block("successful assert_block") {true}
        }
        check_fails("assert_block failed.") {
          assert_block {false}
        }
        check_fails("failed assert_block") {
          assert_block("failed assert_block") {false}
        }
      end
      
      def test_assert
        check_nothing_fails{assert("a")}
        check_nothing_fails{assert(true)}
        check_nothing_fails{assert(true, "successful assert")}
        check_fails("<nil> is not true."){assert(nil)}
        check_fails("<false> is not true."){assert(false)}
        check_fails("failed assert.\n<false> is not true."){assert(false, "failed assert")}
      end
      
      def test_assert_equal
        check_nothing_fails {
          assert_equal("string1", "string1")
        }
        check_nothing_fails {
          assert_equal("string1", "string1", "successful assert_equal")
        }

        message = <<-EOM.chomp
<"string1"> expected but was
<"string2">.

diff:
- string1
?       ^
+ string2
?       ^
EOM
        check_fails(message) {
          assert_equal("string1", "string2")
        }

        message = <<-EOM.chomp
failed assert_equal.
<"string1"> expected but was
<"string2">.

diff:
- string1
?       ^
+ string2
?       ^
EOM
        check_fails(message) {
          assert_equal("string1", "string2", "failed assert_equal")
        }

        message = <<-EOM.chomp
<"111111"> expected but was
<111111>.

diff:
- "111111"
? -      -
+ 111111
EOM
        check_fails(message) do
          assert_equal("111111", 111111)
        end
      end

      def test_assert_equal_with_long_line
        expected = ["0123456789",
                    "1123456789",
                    "2123456789",
                    "3123456789",
                    "4123456789",
                    "5123456789",
                    "6123456789",
                    "7123456789",
                    "8123456789"].join
        actual =   ["0000000000",
                    "1123456789",
                    "2123456789",
                    "3123456789",
                    "4123456789",
                    "5123456789",
                    "6123456789",
                    "7123456789",
                    "8123456789"].join
        message = <<-EOM.chomp
<"#{expected}"> expected but was
<"#{actual}">.

diff:
- #{expected}
?  ^^^^^^^^^
+ #{actual}
?  ^^^^^^^^^

folded diff:
- 012345678911234567892123456789312345678941234567895123456789612345678971234567
?  ^^^^^^^^^
+ 000000000011234567892123456789312345678941234567895123456789612345678971234567
?  ^^^^^^^^^
  898123456789
EOM
        check_fails(message) do
          assert_equal(expected, actual)
        end
      end

      def test_assert_equal_for_too_small_difference
        message = <<-EOM.chomp
<1> expected but was
<2>.
EOM
        check_fails(message) do
          assert_equal(1, 2)
        end
      end

      def test_assert_equal_for_same_inspected_objects
        now = Time.now
        now_without_usec = Time.at(now.to_i)
        message = <<-EOM.chomp
<#{now.inspect}> expected but was
<#{now.inspect}>.
EOM
        check_fails(message) do
          assert_equal(now, now_without_usec)
        end
      end

      def test_assert_equal_with_multi_lines_result
        message = <<-EOM.chomp
<#{"a\nb".inspect}> expected but was
<#{"x".inspect}>.

diff:
+ x
- a
- b
EOM
        check_fails(message) do
          assert_equal("a\nb", "x")
        end
      end

      def test_assert_equal_with_large_string
        message = <<-EOM.chomp
<#{("a\n" + "x" * 297).inspect}> expected but was
<#{"x".inspect}>.

diff:
+ x
- a
- #{"x" * 297}

folded diff:
+ x
- a
- #{"x" * 78}
- #{"x" * 78}
- #{"x" * 78}
- #{"x" * 63}
EOM
        check_fails(message) do
          assert_equal("a\n" + "x" * 297, "x")
        end

        message = <<-EOM.chomp
<#{("a\n" + "x" * 298).inspect}> expected but was
<#{"x".inspect}>.
EOM
        check_fails(message) do
          assert_equal("a\n" + "x" * 298, "x")
        end
      end

      def test_assert_raise_success
        return_value = nil
        check_nothing_fails(true) do
          return_value = assert_raise(RuntimeError) do
            raise "Error"
          end
        end
        check(return_value.kind_of?(Exception),
              "Should have returned the exception " +
              "from a successful assert_raise")
        check(return_value.message == "Error",
              "Should have returned the correct exception " +
              "from a successful assert_raise")

        check_nothing_fails(true) do
          assert_raise(ArgumentError, "successful assert_raise") do
            raise ArgumentError.new("Error")
          end
        end

        check_nothing_fails(true) do
          assert_raise(RuntimeError) do
            raise "Error"
          end
        end

        check_nothing_fails(true) do
          assert_raise(RuntimeError, "successful assert_raise") do
            raise "Error"
          end
        end

        check_nothing_fails(true) do
          assert_raise do
            raise Exception, "Any exception"
          end
        end
      end

      def test_assert_raise_fail
        check_fails("<RuntimeError> exception expected but none was thrown.") do
          assert_raise(RuntimeError) do
            1 + 1
          end
        end

        message = <<-EOM
failed assert_raise.
<ArgumentError> exception expected but was
Class: <RuntimeError>
Message: <"Error">
EOM
        check_fails(/\A#{message}#{BACKTRACE_RE}\Z/m) do
          assert_raise(ArgumentError, "failed assert_raise") do
            raise "Error"
          end
        end

        message = <<-EOM
Should expect a class of exception, Object.
<false> is not true.
EOM
        check_fails(message.chomp) do
          assert_nothing_raised(Object) do
            1 + 1
          end
        end
      end

      def test_assert_raise_module
        exceptions = [ArgumentError, TypeError]
        modules = [Math, Comparable]
        rescues = exceptions + modules

        exceptions.each do |exc|
          return_value = nil
          check_nothing_fails(true) do
            return_value = assert_raise(*rescues) do
              raise exc, "Error"
            end
          end
          check(return_value.instance_of?(exc),
                "Should have returned #{exc} but was #{return_value.class}")
          check(return_value.message == "Error",
                "Should have returned the correct exception " +
                "from a successful assert_raise")
        end

        modules.each do |mod|
          return_value = nil
          check_nothing_fails(true) do
            return_value = assert_raise(*rescues) do
              raise Exception.new("Error").extend(mod)
            end
          end
          check(mod === return_value,
                "Should have returned #{mod}")
          check(return_value.message == "Error",
                "Should have returned the correct exception " +
                "from a successful assert_raise")
        end

        check_fails("<[ArgumentError, TypeError, Math, Comparable]> exception " +
                    "expected but none was thrown.") do
          assert_raise(*rescues) do
            1 + 1
          end
        end

        message = <<-EOM
failed assert_raise.
<[ArgumentError, TypeError]> exception expected but was
Class: <RuntimeError>
Message: <"Error">
EOM
        message = Regexp.escape(message)
        check_fails(/\A#{message}#{BACKTRACE_RE}\z/m) do
          assert_raise(ArgumentError, TypeError, "failed assert_raise") do
            raise "Error"
          end
        end
      end

      def test_assert_raise_instance
        return_value = nil
        check_nothing_fails(true) do
          return_value = assert_raise(RuntimeError.new("Error")) do
            raise "Error"
          end
        end
        check(return_value.kind_of?(Exception),
              "Should have returned the exception " +
              "from a successful assert_raise")
        check(return_value.message == "Error",
              "Should have returned the correct exception " +
              "from a successful assert_raise")

        message = <<-EOM
<RuntimeError("XXX")> exception expected but was
Class: <RuntimeError>
Message: <"Error">
EOM
        message = Regexp.escape(message)
        check_fails(/\A#{message}#{BACKTRACE_RE}\z/) do
          return_value = assert_raise(RuntimeError.new("XXX")) do
            raise "Error"
          end
        end

        different_error_class = Class.new(StandardError)
        message = <<-EOM
<\#<Class:[xa-f\\d]+>\\("Error"\\)> exception expected but was
Class: <RuntimeError>
Message: <"Error">
EOM
        check_fails(/\A#{message}#{BACKTRACE_RE}\z/) do
          assert_raise(different_error_class.new("Error")) do
            raise "Error"
          end
        end

        different_error = different_error_class.new("Error")
        def different_error.inspect
          "DifferentError: \"Error\""
        end
        message = <<-EOM
<\DifferentError: \\"Error\\"> exception expected but was
Class: <RuntimeError>
Message: <"Error">
EOM
        check_fails(/\A#{message}#{BACKTRACE_RE}\z/) do
          assert_raise(different_error) do
            raise "Error"
          end
        end

        check_nothing_fails(true) do
          assert_raise(different_error_class.new("Error"),
                       RuntimeError.new("Error"),
                       RuntimeError.new("XXX")) do
            raise "Error"
          end
        end
      end

      def test_assert_instance_of
        check_nothing_fails {
          assert_instance_of(String, "string")
        }
        check_nothing_fails {
          assert_instance_of(String, "string", "successful assert_instance_of")
        }
        check_nothing_fails {
          assert_instance_of(String, "string", "successful assert_instance_of")
        }
        check_fails(%Q{<"string"> expected to be an instance of\n<Hash> but was\n<String>.}) {
          assert_instance_of(Hash, "string")
        }
        check_fails(%Q{failed assert_instance_of.\n<"string"> expected to be an instance of\n<Hash> but was\n<String>.}) {
          assert_instance_of(Hash, "string", "failed assert_instance_of")
        }

        check_nothing_fails do
          assert_instance_of([Fixnum, NilClass], 100)
        end
        check_fails(%Q{<"string"> expected to be an instance of\n[<Fixnum>, <NilClass>] but was\n<String>.}) do
          assert_instance_of([Fixnum, NilClass], "string")
        end
        check_fails(%Q{<100> expected to be an instance of\n[<Numeric>, <NilClass>] but was\n<Fixnum>.}) do
          assert_instance_of([Numeric, NilClass], 100)
        end
      end

      def test_assert_nil
        check_nothing_fails {
          assert_nil(nil)
        }
        check_nothing_fails {
          assert_nil(nil, "successful assert_nil")
        }
        check_nothing_fails {
          assert_nil(nil, "successful assert_nil")
        }
        check_fails(%Q{<"string"> expected to be nil.}) {
          assert_nil("string")
        }
        check_fails(%Q{failed assert_nil.\n<"string"> expected to be nil.}) {
          assert_nil("string", "failed assert_nil")
        }
      end
      
      def test_assert_not_nil
        check_nothing_fails{assert_not_nil(false)}
        check_nothing_fails{assert_not_nil(false, "message")}
        check_fails("<nil> expected to not be nil."){assert_not_nil(nil)}
        check_fails("message.\n<nil> expected to not be nil.") {assert_not_nil(nil, "message")}
      end
        
      def test_assert_kind_of
        check_nothing_fails {
          assert_kind_of(Module, Array)
        }
        check_nothing_fails {
          assert_kind_of(Object, "string", "successful assert_kind_of")
        }
        check_nothing_fails {
          assert_kind_of(Object, "string", "successful assert_kind_of")
        }
        check_nothing_fails {
          assert_kind_of(Comparable, 1)
        }
        check_fails(%Q{<"string"> expected to be kind_of?\n<Class> but was\n<String>.}) {
          assert_kind_of(Class, "string")
        }
        check_fails(%Q{failed assert_kind_of.\n<"string"> expected to be kind_of?\n<Class> but was\n<String>.}) {
          assert_kind_of(Class, "string", "failed assert_kind_of")
        }

        check_nothing_fails do
          assert_kind_of([Fixnum, NilClass], 100)
        end
        check_fails(%Q{<"string"> expected to be kind_of?\n[<Fixnum>, <NilClass>] but was\n<String>.}) do
          assert_kind_of([Fixnum, NilClass], "string")
        end
      end

      def test_assert_match
        check_nothing_fails {
          assert_match(/strin./, "string")
        }
        check_nothing_fails {
          assert_match("strin", "string")
        }
        check_nothing_fails {
          assert_match(/strin./, "string", "successful assert_match")
        }
        check_nothing_fails {
          assert_match(/strin./, "string", "successful assert_match")
        }
        check_fails(%Q{<"string"> expected to be =~\n</slin./>.}) {
          assert_match(/slin./, "string")
        }
        check_fails(%Q{<"string"> expected to be =~\n</strin\\./>.}) {
          assert_match("strin.", "string")
        }
        check_fails(%Q{failed assert_match.\n<"string"> expected to be =~\n</slin./>.}) {
          assert_match(/slin./, "string", "failed assert_match")
        }
      end
      
      def test_assert_same
        thing = "thing"
        check_nothing_fails {
          assert_same(thing, thing)
        }
        check_nothing_fails {
          assert_same(thing, thing, "successful assert_same")
        }
        check_nothing_fails {
          assert_same(thing, thing, "successful assert_same")
        }
        thing2 = "thing"
        check_fails(%Q{<"thing">\nwith id <#{thing.__id__}> expected to be equal? to\n<"thing">\nwith id <#{thing2.__id__}>.}) {
          assert_same(thing, thing2)
        }
        check_fails(%Q{failed assert_same.\n<"thing">\nwith id <#{thing.__id__}> expected to be equal? to\n<"thing">\nwith id <#{thing2.__id__}>.}) {
          assert_same(thing, thing2, "failed assert_same")
        }
      end
      
      def test_assert_nothing_raised
        check_nothing_fails {
          assert_nothing_raised {
            1 + 1
          }
        }
        check_nothing_fails {
          assert_nothing_raised("successful assert_nothing_raised") {
            1 + 1
          }
        }
        check_nothing_fails {
          assert_nothing_raised("successful assert_nothing_raised") {
            1 + 1
          }
        }
        check_nothing_fails {
          begin
            assert_nothing_raised(RuntimeError, StandardError, Comparable, "successful assert_nothing_raised") {
              raise ZeroDivisionError.new("ArgumentError")
            }
          rescue ZeroDivisionError
          end
        }
        check_fails("Should expect a class of exception, Object.\n<false> is not true.") {
          assert_nothing_raised(Object) {
            1 + 1
          }
        }
        check_fails(%r{\AException raised:\nClass: <RuntimeError>\nMessage: <"Error">\n---Backtrace---\n.+\n---------------\Z}m) {
          assert_nothing_raised {
            raise "Error"
          }
        }
        check_fails(%r{\Afailed assert_nothing_raised\.\nException raised:\nClass: <RuntimeError>\nMessage: <"Error">\n---Backtrace---\n.+\n---------------\Z}m) {
          assert_nothing_raised("failed assert_nothing_raised") {
            raise "Error"
          }
        }
        check_fails(%r{\AException raised:\nClass: <RuntimeError>\nMessage: <"Error">\n---Backtrace---\n.+\n---------------\Z}m) {
          assert_nothing_raised(StandardError, RuntimeError) {
            raise "Error"
          }
        }
        check_fails("Failure.") do
          assert_nothing_raised do
            flunk("Failure")
          end
        end
      end
      
      def test_flunk
        check_fails("Flunked.") {
          flunk
        }
        check_fails("flunk message.") {
          flunk("flunk message")
        }
      end
      
      def test_assert_not_same
        thing = "thing"
        thing2 = "thing"
        check_nothing_fails {
          assert_not_same(thing, thing2)
        }
        check_nothing_fails {
          assert_not_same(thing, thing2, "message")
        }
        check_fails(%Q{<"thing">\nwith id <#{thing.__id__}> expected to not be equal? to\n<"thing">\nwith id <#{thing.__id__}>.}) {
          assert_not_same(thing, thing)
        }
        check_fails(%Q{message.\n<"thing">\nwith id <#{thing.__id__}> expected to not be equal? to\n<"thing">\nwith id <#{thing.__id__}>.}) {
          assert_not_same(thing, thing, "message")
        }
      end
      
      def test_assert_not_equal
        check_nothing_fails {
          assert_not_equal("string1", "string2")
        }
        check_nothing_fails {
          assert_not_equal("string1", "string2", "message")
        }
        check_fails(%Q{<"string"> expected to be != to\n<"string">.}) {
          assert_not_equal("string", "string")
        }
        check_fails(%Q{message.\n<"string"> expected to be != to\n<"string">.}) {
          assert_not_equal("string", "string", "message")
        }
      end
      
      def test_assert_no_match
        check_nothing_fails{assert_no_match(/sling/, "string")}
        check_nothing_fails{assert_no_match(/sling/, "string", "message")}
        check_fails(%Q{The first argument to assert_no_match should be a Regexp.\n<"asdf"> expected to be an instance of\n<Regexp> but was\n<String>.}) do
          assert_no_match("asdf", "asdf")
        end
        check_fails(%Q{</string/> expected to not match\n<"string">.}) do
          assert_no_match(/string/, "string")
        end
        check_fails(%Q{message.\n</string/> expected to not match\n<"string">.}) do
          assert_no_match(/string/, "string", "message")
        end
      end
      
      def test_assert_throw
        check_nothing_fails do
          assert_throw(:thing, "message") do
            throw :thing
          end
        end

        tag = :thing2
        check_fails("message.\n" +
                    "<:thing> expected to be thrown but\n" +
                    "<#{inspect_tag(tag)}> was thrown.") do
          assert_throw(:thing, "message") do
            throw :thing2
          end
        end
        check_fails("message.\n" +
                    "<:thing> should have been thrown.") do
          assert_throw(:thing, "message") do
            1 + 1
          end
        end
      end
      
      def test_assert_nothing_thrown
        check_nothing_fails do
          assert_nothing_thrown("message") do
            1 + 1
          end
        end

        tag = :thing
        inspected = inspect_tag(tag)
        check_fails("message.\n" +
                    "<#{inspected}> was thrown when nothing was expected.") do
          assert_nothing_thrown("message") do
            throw tag
          end
        end
      end
      
      def test_assert_operator
        check_nothing_fails {
          assert_operator("thing", :==, "thing", "message")
        }
        check_fails(%Q{<0.15>\ngiven as the operator for #assert_operator must be a Symbol or #respond_to?(:to_str).}) do
          assert_operator("thing", 0.15, "thing")
        end
        check_fails(%Q{message.\n<"thing1"> expected to be\n==\n<"thing2">.}) {
          assert_operator("thing1", :==, "thing2", "message")
        }
      end
      
      def test_assert_respond_to
        check_nothing_fails {
          assert_respond_to("thing", :to_s, "message")
        }
        check_nothing_fails {
          assert_respond_to("thing", "to_s", "message")
        }
        check_fails("<0.15>.kind_of?(Symbol) or\n" +
                    "<0.15>.respond_to?(:to_str) expected") {
          assert_respond_to("thing", 0.15)
        }
        check_fails("message.\n" +
                    "<:symbol>.respond_to?(:non_existent) expected\n" +
                    "(Class: <Symbol>)") {
          assert_respond_to(:symbol, :non_existent, "message")
        }
      end
      
      def test_assert_in_delta
        check_nothing_fails {
          assert_in_delta(1.4, 1.4, 0)
        }
        check_nothing_fails {
          assert_in_delta(0.5, 0.4, 0.1, "message")
        }
        check_nothing_fails {
          float_thing = Object.new
          def float_thing.to_f
            0.2
          end
          assert_in_delta(0.1, float_thing, 0.1)
        }
        check_fails("message.\n<0.5> and\n<0.4> expected to be within\n<0.05> of each other.") {
          assert_in_delta(0.5, 0.4, 0.05, "message")
        }
        object = Object.new
        check_fails("The arguments must respond to to_f; " +
                    "the first float did not.\n" +
                    "<#{object.inspect}>.respond_to?(:to_f) expected\n" +
                    "(Class: <Object>)") {
          assert_in_delta(object, 0.4, 0.1)
        }
        check_fails("The delta should not be negative.\n" +
                    "<-0.1> expected to be\n>=\n<0.0>.") {
          assert_in_delta(0.5, 0.4, -0.1, "message")
        }
      end
      
      def test_assert_send
        object = Object.new
        class << object
          private
          def return_argument(argument, bogus)
            return argument
          end
        end
        check_nothing_fails {
          assert_send([object, :return_argument, true, "bogus"], "message")
        }
        check_fails(%r{\Amessage\.\n<.+> expected to respond to\n<return_argument\(\[false, "bogus"\]\)> with a true value.\Z}) {
          assert_send([object, :return_argument, false, "bogus"], "message")
        }
      end
      
      def test_condition_invariant
        object = Object.new
        def object.inspect
          @changed = true
        end
        def object.==(other)
          @changed ||= false
          return (!@changed)
        end
        check_nothing_fails do
          assert_equal(object, object, "message")
        end
      end

      def test_assert_boolean
        check_nothing_fails do
          assert_boolean(true)
        end
        check_nothing_fails do
          assert_boolean(false)
        end

        check_fails("<true> or <false> expected but was\n<1>") do
          assert_boolean(1)
        end

        check_fails("<true> or <false> expected but was\n<nil>") do
          assert_boolean(nil)
        end

        check_fails("message.\n<true> or <false> expected but was\n<\"XXX\">") do
          assert_boolean("XXX", "message")
        end
      end

      def test_assert_true
        check_nothing_fails do
          assert_true(true)
        end

        check_fails("<true> expected but was\n<false>") do
          assert_true(false)
        end

        check_fails("<true> expected but was\n<1>") do
          assert_true(1)
        end

        check_fails("message.\n<true> expected but was\n<nil>") do
          assert_true(nil, "message")
        end
      end

      def test_assert_false
        check_nothing_fails do
          assert_false(false)
        end

        check_fails("<false> expected but was\n<true>") do
          assert_false(true)
        end

        check_fails("<false> expected but was\n<nil>") do
          assert_false(nil)
        end

        check_fails("message.\n<false> expected but was\n<:false>") do
          assert_false(:false, "message")
        end
      end

      def test_assert_compare
        check_nothing_fails do
          assert_compare(1.4, "<", 10.0)
        end

        check_nothing_fails do
          assert_compare(2, "<=", 2)
        end

        check_nothing_fails do
          assert_compare(14, ">=", 10.0)
        end

        check_nothing_fails do
          assert_compare(14, ">", 13.9)
        end

        expected_message = <<-EOM
<15> < <10> should be true
<15> expected less than
<10>.
EOM
        check_fails(expected_message.chomp) do
          assert_compare(15, "<", 10)
        end

        expected_message = <<-EOM
<15> <= <10> should be true
<15> expected less than or equal to
<10>.
EOM
        check_fails(expected_message.chomp) do
          assert_compare(15, "<=", 10)
        end

        expected_message = <<-EOM
<10> > <15> should be true
<10> expected greater than
<15>.
EOM
        check_fails(expected_message.chomp) do
          assert_compare(10, ">", 15)
        end

        expected_message = <<-EOM
<10> >= <15> should be true
<10> expected greater than or equal to
<15>.
EOM
        check_fails(expected_message.chomp) do
          assert_compare(10, ">=", 15)
        end
      end

      def test_assert_fail_assertion
        check_nothing_fails do
          assert_fail_assertion do
            flunk
          end
        end

        check_fails("Failed assertion was expected.") do
          assert_fail_assertion do
          end
        end
      end

      def test_assert_raise_message
        check_nothing_fails do
          assert_raise_message("Raise!") do
            raise "Raise!"
          end
        end

        check_nothing_fails do
          assert_raise_message("Raise!") do
            raise Exception, "Raise!"
          end
        end

        check_nothing_fails do
          assert_raise_message(/raise/i) do
            raise "Raise!"
          end
        end

        expected_message = <<-EOM
<"Expected message"> exception message expected but was
<"Actual message">.
EOM
        check_fails(expected_message.chomp) do
          assert_raise_message("Expected message") do
            raise "Actual message"
          end
        end

        expected_message = <<-EOM
<"Expected message"> exception message expected but none was thrown.
EOM
        check_fails(expected_message.chomp) do
          assert_raise_message("Expected message") do
          end
        end
      end

      def test_assert_raise_kind_of
        check_nothing_fails(true) do
          assert_raise_kind_of(SystemCallError) do
            raise Errno::EACCES
          end
        end

        expected_message = <<-EOM
<SystemCallError> family exception expected but was
Class: <RuntimeError>
Message: <"XXX">
---Backtrace---
EOM
        check_fails(/\A#{Regexp.escape(expected_message)}(?m).+\z/) do
          assert_raise_kind_of(SystemCallError) do
            raise RuntimeError, "XXX"
          end
        end
      end

      def test_assert_const_defined
        check_nothing_fails do
          assert_const_defined(Test, :Unit)
        end

        check_nothing_fails do
          assert_const_defined(Test, "Unit")
        end

        check_fails("<Test>.const_defined?(<:Nonexistence>) expected.") do
          assert_const_defined(Test, :Nonexistence)
        end
      end

      def test_assert_not_const_defined
        check_nothing_fails do
          assert_not_const_defined(Test, :Nonexistence)
        end

        check_fails("!<Test>.const_defined?(<:Unit>) expected.") do
          assert_not_const_defined(Test, :Unit)
        end

        check_fails("!<Test>.const_defined?(<\"Unit\">) expected.") do
          assert_not_const_defined(Test, "Unit")
        end
      end

      def test_assert_predicate
        check_nothing_fails do
          assert_predicate([], :empty?)
        end

        check_fails("<[1]>.empty? is true value expected but was\n<false>") do
          assert_predicate([1], :empty?)
        end

        check_fails("<[1]>.respond_to?(:nonexistent?) expected\n" +
                    "(Class: <Array>)") do
          assert_predicate([1], :nonexistent?)
        end
      end

      def test_assert_not_predicate
        check_nothing_fails do
          assert_not_predicate([1], :empty?)
        end

        check_fails("<[]>.empty? is false value expected but was\n<true>") do
          assert_not_predicate([], :empty?)
        end

        check_fails("<[]>.respond_to?(:nonexistent?) expected\n" +
                    "(Class: <Array>)") do
          assert_not_predicate([], :nonexistent?)
        end
      end

      private
      def add_failure(message, location=caller)
        unless @catch_assertions
          super
        end
      end

      def add_assertion
        if @catch_assertions
          @actual_assertion_count += 1
        else
          super
        end
      end
    end
  end
end
