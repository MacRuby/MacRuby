# Author:: Nathaniel Talbott.
# Copyright:: Copyright (c) 2000-2003 Nathaniel Talbott. All rights reserved.
#             Copyright (c) 2009 Kouhei Sutou.
# License:: Ruby license.

require 'test/unit/assertionfailederror'
require 'test/unit/util/backtracefilter'
require 'test/unit/util/method-owner-finder'
require 'test/unit/diff'

module Test
  module Unit

    ##
    # Test::Unit::Assertions contains the standard Test::Unit assertions.
    # Assertions is included in Test::Unit::TestCase.
    #
    # To include it in your own code and use its functionality, you simply
    # need to rescue Test::Unit::AssertionFailedError. Additionally you may
    # override add_assertion to get notified whenever an assertion is made.
    #
    # Notes:
    # * The message to each assertion, if given, will be propagated with the
    #   failure.
    # * It is easy to add your own assertions based on assert_block().
    #
    # = Example Custom Assertion
    #
    #   def deny(boolean, message = nil)
    #     message = build_message message, '<?> is not false or nil.', boolean
    #     assert_block message do
    #       not boolean
    #     end
    #   end

    module Assertions

      ##
      # The assertion upon which all other assertions are based. Passes if the
      # block yields true.
      #
      # Example:
      #   assert_block "Couldn't do the thing" do
      #     do_the_thing
      #   end

      public
      def assert_block(message="assert_block failed.") # :yields: 
        _wrap_assertion do
          if (! yield)
            raise AssertionFailedError.new(message.to_s)
          end
        end
      end

      ##
      # Asserts that +boolean+ is not false or nil.
      #
      # Example:
      #   assert [1, 2].include?(5)

      public
      def assert(boolean, message=nil)
        _wrap_assertion do
          assert_block("assert should not be called with a block.") { !block_given? }
          assert_block(build_message(message, "<?> is not true.", boolean)) { boolean }
        end
      end

      ##
      # Passes if +expected+ == +actual.
      #
      # Note that the ordering of arguments is important, since a helpful
      # error message is generated when this one fails that tells you the
      # values of expected and actual.
      #
      # Example:
      #   assert_equal 'MY STRING', 'my string'.upcase

      public
      def assert_equal(expected, actual, message=nil)
        diff = AssertionMessage.delayed_diff(expected, actual)
        full_message = build_message(message, <<EOT, expected, actual, diff)
<?> expected but was
<?>.?
EOT
        assert_block(full_message) { expected == actual }
      end

      ##
      # Passes if the block raises one of the expected
      # exceptions. When an expected exception is an Exception
      # object, passes if expected_exception == actual_exception.
      #
      # Example:
      #   assert_raise(RuntimeError, LoadError) do
      #     raise 'Boom!!!'
      #   end # -> pass
      #
      #   assert_raise do
      #     raise Exception, 'Any exception should be raised!!!'
      #   end # -> pass
      #
      #   assert_raise(RuntimeError.new("XXX")) {raise "XXX"} # -> pass
      #   assert_raise(MyError.new("XXX"))      {raise "XXX"} # -> fail
      #   assert_raise(RuntimeError.new("ZZZ")) {raise "XXX"} # -> fail
      public
      def assert_raise(*args, &block)
        assert_expected_exception = Proc.new do |*_args|
          message, assert_exception_helper, actual_exception = _args
          expected = assert_exception_helper.expected_exceptions
          full_message = build_message(message,
                                       "<?> exception expected but was\n?",
                                       expected, actual_exception)
          assert_block(full_message) do
            expected == [] or assert_exception_helper.expected?(actual_exception)
          end
        end
        _assert_raise(assert_expected_exception, *args, &block)
      end

      ##
      # Alias of assert_raise.
      #
      # Will be deprecated in 1.9, and removed in 2.0.

      public
      def assert_raises(*args, &block)
        assert_raise(*args, &block)
      end

      ##
      # Passes if the block raises one of the given
      # exceptions or sub exceptions of the given exceptions.
      #
      # Example:
      #   assert_raise_kind_of(SystemCallError) do
      #     raise Errno::EACCES
      #   end
      def assert_raise_kind_of(*args, &block)
        assert_expected_exception = Proc.new do |*_args|
          message, assert_exception_helper, actual_exception = _args
          expected = assert_exception_helper.expected_exceptions
          full_message = build_message(message,
                                       "<?> family exception expected " +
                                       "but was\n?",
                                       expected, actual_exception)
          assert_block(full_message) do
            assert_exception_helper.expected?(actual_exception, :kind_of?)
          end
        end
        _assert_raise(assert_expected_exception, *args, &block)
      end


      ##
      # Passes if +object+.instance_of?(+klass+). When +klass+ is
      # an array of classes, it passes if any class
      # satisfies +object.instance_of?(class).
      #
      # Example:
      #   assert_instance_of(String, 'foo')            # -> pass
      #   assert_instance_of([Fixnum, NilClass], 100)  # -> pass
      #   assert_instance_of([Numeric, NilClass], 100) # -> fail

      public
      def assert_instance_of(klass, object, message="")
        _wrap_assertion do
          klasses = nil
          klasses = klass if klass.is_a?(Array)
          assert_block("The first parameter to assert_instance_of should be " +
                       "a Class or an Array of Class.") do
            if klasses
              klasses.all? {|k| k.is_a?(Class)}
            else
              klass.is_a?(Class)
            end
          end
          klass_message = AssertionMessage.maybe_container(klass) do |value|
            "<#{value}>"
          end
          full_message = build_message(message, <<EOT, object, klass_message, object.class)
<?> expected to be an instance of
? but was
<?>.
EOT
          assert_block(full_message) do
            if klasses
              klasses.any? {|k| object.instance_of?(k)}
            else
              object.instance_of?(klass)
            end
          end
        end
      end

      ##
      # Passes if +object+ is nil.
      #
      # Example:
      #   assert_nil [1, 2].uniq!

      public
      def assert_nil(object, message="")
        full_message = build_message(message, <<EOT, object)
<?> expected to be nil.
EOT
        assert_block(full_message) { object.nil? }
      end

      ##
      # Passes if +object+.kind_of?(+klass+). When +klass+ is
      # an array of classes or modules, it passes if any
      # class or module satisfies +object.kind_of?(class_or_module).
      #
      # Example:
      #   assert_kind_of(Object, 'foo')                # -> pass
      #   assert_kind_of([Fixnum, NilClass], 100)      # -> pass
      #   assert_kind_of([Fixnum, NilClass], "string") # -> fail

      public
      def assert_kind_of(klass, object, message="")
        _wrap_assertion do
          klasses = nil
          klasses = klass if klass.is_a?(Array)
          assert_block("The first parameter to assert_kind_of should be " +
                       "a kind_of Module or an Array of a kind_of Module.") do
            if klasses
              klasses.all? {|k| k.kind_of?(Module)}
            else
              klass.kind_of?(Module)
            end
          end
          klass_message = AssertionMessage.maybe_container(klass) do |value|
            "<#{value}>"
          end
          full_message = build_message(message,
                                       "<?> expected to be kind_of\\?\n" +
                                       "? but was\n" +
                                       "<?>.",
                                       object,
                                       klass_message,
                                       object.class)
          assert_block(full_message) do
            if klasses
              klasses.any? {|k| object.kind_of?(k)}
            else
              object.kind_of?(klass)
            end
          end
        end
      end

      ##
      # Passes if +object+ .respond_to? +method+
      #
      # Example:
      #   assert_respond_to 'bugbear', :slice

      public
      def assert_respond_to(object, method, message="")
        _wrap_assertion do
          full_message = build_message(message,
                                       "<?>.kind_of\\?(Symbol) or\n" +
                                       "<?>.respond_to\\?(:to_str) expected",
                                       method, method)
          assert_block(full_message) do
            method.kind_of?(Symbol) or method.respond_to?(:to_str)
          end
          full_message = build_message(message,
                                       "<?>.respond_to\\?(?) expected\n" +
                                       "(Class: <?>)",
                                       object, method, object.class)
          assert_block(full_message) {object.respond_to?(method)}
        end
      end

      ##
      # Passes if +string+ =~ +pattern+.
      #
      # Example:
      #   assert_match(/\d+/, 'five, 6, seven')

      public
      def assert_match(pattern, string, message="")
        _wrap_assertion do
          pattern = case(pattern)
            when String
              Regexp.new(Regexp.escape(pattern))
            else
              pattern
          end
          full_message = build_message(message, "<?> expected to be =~\n<?>.", string, pattern)
          assert_block(full_message) { string =~ pattern }
        end
      end

      ##
      # Passes if +actual+ .equal? +expected+ (i.e. they are the same
      # instance).
      #
      # Example:
      #   o = Object.new
      #   assert_same o, o

      public
      def assert_same(expected, actual, message="")
        full_message = build_message(message, <<EOT, expected, expected.__id__, actual, actual.__id__)
<?>
with id <?> expected to be equal\\? to
<?>
with id <?>.
EOT
        assert_block(full_message) { actual.equal?(expected) }
      end

      ##
      # Compares the +object1+ with +object2+ using +operator+.
      #
      # Passes if object1.__send__(operator, object2) is true.
      #
      # Example:
      #   assert_operator 5, :>=, 4

      public
      def assert_operator(object1, operator, object2, message="")
        _wrap_assertion do
          full_message = build_message(nil, "<?>\ngiven as the operator for #assert_operator must be a Symbol or #respond_to\\?(:to_str).", operator)
          assert_block(full_message){operator.kind_of?(Symbol) || operator.respond_to?(:to_str)}
          full_message = build_message(message, <<EOT, object1, AssertionMessage.literal(operator), object2)
<?> expected to be
?
<?>.
EOT
          assert_block(full_message) { object1.__send__(operator, object2) }
        end
      end

      ##
      # Passes if block does not raise an exception.
      #
      # Example:
      #   assert_nothing_raised do
      #     [1, 2].uniq
      #   end

      public
      def assert_nothing_raised(*args)
        _wrap_assertion do
          if args.last.is_a?(String)
            message = args.pop
          else
            message = ""
          end

          assert_exception_helper = AssertExceptionHelper.new(self, args)
          begin
            yield
          rescue Exception => e
            if ((args.empty? && !e.instance_of?(AssertionFailedError)) ||
                assert_exception_helper.expected?(e))
              failure_message = build_message(message, "Exception raised:\n?", e)
              assert_block(failure_message) {false}
            else
              raise
            end
          end
          nil
        end
      end

      ##
      # Flunk always fails.
      #
      # Example:
      #   flunk 'Not done testing yet.'

      public
      def flunk(message="Flunked")
        assert_block(build_message(message)){false}
      end

      ##
      # Passes if ! +actual+ .equal? +expected+
      #
      # Example:
      #   assert_not_same Object.new, Object.new

      public
      def assert_not_same(expected, actual, message="")
        full_message = build_message(message, <<EOT, expected, expected.__id__, actual, actual.__id__)
<?>
with id <?> expected to not be equal\\? to
<?>
with id <?>.
EOT
        assert_block(full_message) { !actual.equal?(expected) }
      end

      ##
      # Passes if +expected+ != +actual+
      #
      # Example:
      #   assert_not_equal 'some string', 5

      public
      def assert_not_equal(expected, actual, message="")
        full_message = build_message(message, "<?> expected to be != to\n<?>.", expected, actual)
        assert_block(full_message) { expected != actual }
      end

      ##
      # Passes if ! +object+ .nil?
      #
      # Example:
      #   assert_not_nil '1 two 3'.sub!(/two/, '2')

      public
      def assert_not_nil(object, message="")
        full_message = build_message(message, "<?> expected to not be nil.", object)
        assert_block(full_message){!object.nil?}
      end

      ##
      # Passes if +regexp+ !~ +string+ 
      #
      # Example:
      #   assert_no_match(/two/, 'one 2 three')

      public
      def assert_no_match(regexp, string, message="")
        _wrap_assertion do
          assert_instance_of(Regexp, regexp, "The first argument to assert_no_match should be a Regexp.")
          full_message = build_message(message, "<?> expected to not match\n<?>.", regexp, string)
          assert_block(full_message) { regexp !~ string }
        end
      end

      UncaughtThrow = {
        NameError => /^uncaught throw \`(.+)\'$/,
        ArgumentError => /^uncaught throw (.+)$/,
        ThreadError => /^uncaught throw \`(.+)\' in thread /
      } #`

      ##
      # Passes if the block throws +expected_object+
      #
      # Example:
      #   assert_throw(:done) do
      #     throw(:done)
      #   end

      public
      def assert_throw(expected_object, message="", &proc)
        _wrap_assertion do
          begin
            catch([]) {}
          rescue TypeError
            assert_instance_of(Symbol, expected_object,
                               "assert_throws expects the symbol that should be thrown for its first argument")
          end
          assert_block("Should have passed a block to assert_throw.") do
            block_given?
          end
          caught = true
          begin
            catch(expected_object) do
              proc.call
              caught = false
            end
            full_message = build_message(message,
                                         "<?> should have been thrown.",
                                         expected_object)
            assert_block(full_message) {caught}
          rescue NameError, ArgumentError, ThreadError => error
            raise unless UncaughtThrow[error.class] =~ error.message
            tag = $1
            tag = tag[1..-1].intern if tag[0, 1] == ":"
            full_message = build_message(message,
                                         "<?> expected to be thrown but\n" +
                                         "<?> was thrown.",
                                         expected_object, tag)
            flunk(full_message)
          end
        end
      end

      ##
      # Alias of assert_throw.
      #
      # Will be deprecated in 1.9, and removed in 2.0.
      def assert_throws(*args, &block)
        assert_throw(*args, &block)
      end

      ##
      # Passes if block does not throw anything.
      #
      # Example:
      #  assert_nothing_thrown do
      #    [1, 2].uniq
      #  end

      public
      def assert_nothing_thrown(message="", &proc)
        _wrap_assertion do
          assert(block_given?, "Should have passed a block to assert_nothing_thrown")
          begin
            proc.call
          rescue NameError, ArgumentError, ThreadError => error
            raise unless UncaughtThrow[error.class] =~ error.message
            tag = $1
            tag = tag[1..-1].intern if tag[0, 1] == ":"
            full_message = build_message(message,
                                         "<?> was thrown when nothing was expected",
                                         tag)
            flunk(full_message)
          end
          assert(true, "Expected nothing to be thrown")
        end
      end

      ##
      # Passes if +expected_float+ and +actual_float+ are equal
      # within +delta+ tolerance.
      #
      # Example:
      #   assert_in_delta 0.05, (50000.0 / 10**6), 0.00001

      public
      def assert_in_delta(expected_float, actual_float, delta, message="")
        _wrap_assertion do
          {expected_float => "first float", actual_float => "second float", delta => "delta"}.each do |float, name|
            assert_respond_to(float, :to_f, "The arguments must respond to to_f; the #{name} did not")
          end
          assert_operator(delta, :>=, 0.0, "The delta should not be negative")
          full_message = build_message(message, <<EOT, expected_float, actual_float, delta)
<?> and
<?> expected to be within
<?> of each other.
EOT
          assert_block(full_message) { (expected_float.to_f - actual_float.to_f).abs <= delta.to_f }
        end
      end

      ##
      # Passes if the method send returns a true value.
      #
      # +send_array+ is composed of:
      # * A receiver
      # * A method
      # * Arguments to the method
      #
      # Example:
      #   assert_send [[1, 2], :include?, 4]

      public
      def assert_send(send_array, message="")
        _wrap_assertion do
          assert_instance_of(Array, send_array, "assert_send requires an array of send information")
          assert(send_array.size >= 2, "assert_send requires at least a receiver and a message name")
          full_message = build_message(message, <<EOT, send_array[0], AssertionMessage.literal(send_array[1].to_s), send_array[2..-1])
<?> expected to respond to
<?(?)> with a true value.
EOT
          assert_block(full_message) { send_array[0].__send__(send_array[1], *send_array[2..-1]) }
        end
      end

      ##
      # Passes if +actual+ is a boolean value.
      #
      # Example:
      #   assert_boolean(true) # -> pass
      #   assert_boolean(nil)  # -> fail
      def assert_boolean(actual, message=nil)
        _wrap_assertion do
          assert_block(build_message(message,
                                     "<true> or <false> expected but was\n<?>",
                                     actual)) do
            [true, false].include?(actual)
          end
        end
      end

      ##
      # Passes if +actual+ is true.
      #
      # Example:
      #   assert_true(true)  # -> pass
      #   assert_true(:true) # -> fail
      def assert_true(actual, message=nil)
        _wrap_assertion do
          assert_block(build_message(message,
                                     "<true> expected but was\n<?>",
                                     actual)) do
            actual == true
          end
        end
      end

      ##
      # Passes if +actual+ is false.
      #
      # Example:
      #   assert_false(false)  # -> pass
      #   assert_false(nil)    # -> fail
      def assert_false(actual, message=nil)
        _wrap_assertion do
          assert_block(build_message(message,
                                     "<false> expected but was\n<?>",
                                     actual)) do
            actual == false
          end
        end
      end

      ##
      # Passes if expression "+expected+ +operator+
      # +actual+" is true.
      #
      # Example:
      #   assert_compare(1, "<", 10)  # -> pass
      #   assert_compare(1, ">=", 10) # -> fail
      def assert_compare(expected, operator, actual, message=nil)
        _wrap_assertion do
          assert_send([["<", "<=", ">", ">="], :include?, operator.to_s])
          case operator.to_s
          when "<"
            operator_description = "less than"
          when "<="
            operator_description = "less than or equal to"
          when ">"
            operator_description = "greater than"
          when ">="
            operator_description = "greater than or equal to"
          end
          template = <<-EOT
<?> #{operator} <?> should be true
<?> expected #{operator_description}
<?>.
EOT
          full_message = build_message(message, template,
                                       expected, actual,
                                       expected, actual)
          assert_block(full_message) do
            expected.send(operator, actual)
          end
        end
      end

      ##
      # Passes if assertion is failed in block.
      #
      # Example:
      #   assert_fail_assertion {assert_equal("A", "B")}  # -> pass
      #   assert_fail_assertion {assert_equal("A", "A")}  # -> fail
      def assert_fail_assertion(message=nil)
        _wrap_assertion do
          full_message = build_message(message,
                                       "Failed assertion was expected.")
          assert_block(full_message) do
            begin
              yield
              false
            rescue AssertionFailedError
              true
            end
          end
        end
      end

      ##
      # Passes if an exception is raised in block and its
      # message is +expected+.
      #
      # Example:
      #   assert_raise_message("exception") {raise "exception"}  # -> pass
      #   assert_raise_message(/exc/i) {raise "exception"}       # -> pass
      #   assert_raise_message("exception") {raise "EXCEPTION"}  # -> fail
      #   assert_raise_message("exception") {}                   # -> fail
      def assert_raise_message(expected, message=nil)
        _wrap_assertion do
          full_message = build_message(message,
                                       "<?> exception message expected " +
                                       "but none was thrown.",
                                       expected)
          exception = nil
          assert_block(full_message) do
            begin
              yield
              false
            rescue Exception => exception
              true
            end
          end

          actual = exception.message
          diff = AssertionMessage.delayed_diff(expected, actual)
          full_message =
            build_message(message,
                          "<?> exception message expected but was\n" +
                          "<?>.?", expected, actual, diff)
          assert_block(full_message) do
            if expected.is_a?(Regexp)
              expected =~ actual
            else
              expected == actual
            end
          end
        end
      end

      ##
      # Passes if +object+.const_defined?(+constant_name+)
      #
      # Example:
      #   assert_const_defined(Test, :Unit)          # -> pass
      #   assert_const_defined(Object, :Nonexistent) # -> fail
      def assert_const_defined(object, constant_name, message=nil)
        _wrap_assertion do
          full_message = build_message(message,
                                       "<?>.const_defined\\?(<?>) expected.",
                                       object, constant_name)
          assert_block(full_message) do
            object.const_defined?(constant_name)
          end
        end
      end

      ##
      # Passes if !+object+.const_defined?(+constant_name+)
      #
      # Example:
      #   assert_not_const_defined(Object, :Nonexistent) # -> pass
      #   assert_not_const_defined(Test, :Unit)          # -> fail
      def assert_not_const_defined(object, constant_name, message=nil)
        _wrap_assertion do
          full_message = build_message(message,
                                       "!<?>.const_defined\\?(<?>) expected.",
                                       object, constant_name)
          assert_block(full_message) do
            !object.const_defined?(constant_name)
          end
        end
      end

      ##
      # Passes if +object+.+predicate+
      #
      # Example:
      #   assert_predicate([], :empty?)  # -> pass
      #   assert_predicate([1], :empty?) # -> fail
      def assert_predicate(object, predicate, message=nil)
        _wrap_assertion do
          assert_respond_to(object, predicate, message)
          actual = object.send(predicate)
          full_message = build_message(message,
                                       "<?>.? is true value expected but was\n" +
                                       "<?>",
                                       object,
                                       AssertionMessage.literal(predicate),
                                       actual)
          assert_block(full_message) do
            actual
          end
        end
      end

      ##
      # Passes if +object+.+predicate+
      #
      # Example:
      #   assert_not_predicate([1], :empty?) # -> pass
      #   assert_not_predicate([], :empty?)  # -> fail
      def assert_not_predicate(object, predicate, message=nil)
        _wrap_assertion do
          assert_respond_to(object, predicate, message)
          actual = object.send(predicate)
          full_message = build_message(message,
                                       "<?>.? is false value expected but was\n" +
                                       "<?>",
                                       object,
                                       AssertionMessage.literal(predicate),
                                       actual)
          assert_block(full_message) do
            not actual
          end
        end
      end

      ##
      # Builds a failure message.  +head+ is added before the +template+ and
      # +arguments+ replaces the '?'s positionally in the template.

      public
      def build_message(head, template=nil, *arguments)
        template &&= template.chomp
        return AssertionMessage.new(head, template, arguments)
      end

      private
      def _wrap_assertion
        @_assertion_wrapped ||= false
        unless (@_assertion_wrapped)
          @_assertion_wrapped = true
          begin
            add_assertion
            return yield
          ensure
            @_assertion_wrapped = false
          end
        else
          return yield
        end
      end
      
      ##
      # Called whenever an assertion is made.  Define this in classes that
      # include Test::Unit::Assertions to record assertion counts.

      private
      def add_assertion
      end

      ##
      # Select whether or not to use the pretty-printer. If this option is set
      # to false before any assertions are made, pp.rb will not be required.

      public
      def self.use_pp=(value)
        AssertionMessage.use_pp = value
      end

      # :stopdoc:
      private
      def _assert_raise(assert_expected_exception, *args, &block)
        _wrap_assertion do
          if args.last.is_a?(String)
            message = args.pop
          else
            message = ""
          end

          assert_exception_helper = AssertExceptionHelper.new(self, args)
          expected = assert_exception_helper.expected_exceptions
          actual_exception = nil
          full_message = build_message(message,
                                       "<?> exception expected " +
                                       "but none was thrown.",
                                       expected)
          assert_block(full_message) do
            begin
              yield
              false
            rescue Exception => actual_exception
              true
            end
          end
          assert_expected_exception.call(message, assert_exception_helper,
                                         actual_exception)
          actual_exception
        end
      end

      class AssertionMessage
        @use_pp = true
        class << self
          attr_accessor :use_pp

          def literal(value)
            Literal.new(value)
          end

          def delayed_literal(&block)
            DelayedLiteral.new(block)
          end

          def maybe_container(value, &formatter)
            MaybeContainer.new(value, &formatter)
          end

          MAX_DIFF_TARGET_STRING_SIZE = 300
          def diff_target_string?(string)
            if string.respond_to?(:bytesize)
              string.bytesize < MAX_DIFF_TARGET_STRING_SIZE
            else
              string.size < MAX_DIFF_TARGET_STRING_SIZE
            end
          end

          def delayed_diff(from, to)
            delayed_literal do
              if !from.is_a?(String) or !to.is_a?(String)
                from = convert(from)
                to = convert(to)
              end

              diff = nil
              diff = "" if !diff_target_string?(from) or !diff_target_string?(to)
              diff ||= Diff.readable(from, to)
              if /^[-+]/ !~ diff
                diff = ""
              elsif /^[ ?]/ =~ diff or /(?:.*\n){2,}/ =~ diff
                diff = "\n\ndiff:\n#{diff}"
              else
                diff = ""
              end

              if Diff.need_fold?(diff)
                folded_diff = Diff.folded_readable(from, to)
                diff << "\n\nfolded diff:\n#{folded_diff}"
              end

              diff
            end
          end

          def convert(object)
            case object
            when Exception
              <<EOM.chop
Class: <#{convert(object.class)}>
Message: <#{convert(object.message)}>
---Backtrace---
#{Util::BacktraceFilter.filter_backtrace(object.backtrace).join("\n")}
---------------
EOM
            else
              if use_pp
                begin
                  require 'pp' unless defined?(PP)
                  return PP.pp(object, '').chomp
                rescue LoadError
                  self.use_pp = false
                end
              end
              object.inspect
            end
          end
        end

        class Literal
          def initialize(value)
            @value = value
          end

          def inspect
            @value.to_s
          end
        end

        class DelayedLiteral
          def initialize(value)
            @value = value
          end

          def inspect
            @value.call.to_s
          end
        end

        class MaybeContainer
          def initialize(value, &formatter)
            @value = value
            @formatter = formatter
          end

          def inspect
            if @value.is_a?(Array)
              values = @value.collect do |value|
                @formatter.call(AssertionMessage.convert(value))
              end
              "[#{values.join(', ')}]"
            else
              @formatter.call(AssertionMessage.convert(@value))
            end
          end
        end

        class Template
          def self.create(string)
            parts = (string ? string.scan(/(?=[^\\])\?|(?:\\\?|[^\?])+/m) : [])
            self.new(parts)
          end

          attr_reader :count

          def initialize(parts)
            @parts = parts
            @count = parts.find_all{|e| e == '?'}.size
          end

          def result(parameters)
            raise "The number of parameters does not match the number of substitutions." if(parameters.size != count)
            params = parameters.dup
            @parts.collect{|e| e == '?' ? params.shift : e.gsub(/\\\?/m, '?')}.join('')
          end
        end

        include Util::BacktraceFilter

        def initialize(head, template_string, parameters)
          @head = head
          @template_string = template_string
          @parameters = parameters
        end

        def convert(object)
          self.class.convert(object)
        end

        def template
          @template ||= Template.create(@template_string)
        end

        def add_period(string)
          (string =~ /\.\Z/ ? string : string + '.')
        end

        def to_s
          message_parts = []
          if (@head)
            head = @head.to_s 
            unless(head.empty?)
              message_parts << add_period(head)
            end
          end
          tail = template.result(@parameters.collect{|e| convert(e)})
          message_parts << tail unless(tail.empty?)
          message_parts.join("\n")
        end
      end

      class AssertExceptionHelper
        class WrappedException
          def initialize(exception)
            @exception = exception
          end

          def inspect
            if default_inspect?
              "#{@exception.class.inspect}(#{@exception.message.inspect})"
            else
              @exception.inspect
            end
          end

          def method_missing(name, *args, &block)
            @exception.send(name, *args, &block)
          end

          private
          def default_inspect?
            inspect_method = @exception.method(:inspect)
            if inspect_method.respond_to?(:owner) and
                inspect_method.owner == Exception
              true
            else
              default_inspect_method = Exception.instance_method(:inspect)
              default_inspect_method.bind(@exception).call == @exception.inspect
            end
          end
        end

        def initialize(test_case, expected_exceptions)
          @test_case = test_case
          @expected_exceptions = expected_exceptions
          @expected_classes, @expected_modules, @expected_objects =
            split_expected_exceptions(expected_exceptions)
        end

        def expected_exceptions
          exceptions = @expected_exceptions.collect do |exception|
            if exception.is_a?(Exception)
              WrappedException.new(exception)
            else
              exception
            end
          end
          if exceptions.size == 1
            exceptions[0]
          else
            exceptions
          end
        end

        def expected?(actual_exception, equality=nil)
          equality ||= :instance_of?
          expected_class?(actual_exception, equality) or
            expected_module?(actual_exception) or
            expected_object?(actual_exception)
        end

        private
        def split_expected_exceptions(expected_exceptions)
          exception_modules = []
          exception_objects = []
          exception_classes = []
          expected_exceptions.each do |exception_type|
            if exception_type.instance_of?(Module)
              exception_modules << exception_type
            elsif exception_type.is_a?(Exception)
              exception_objects << exception_type
            else
              @test_case.send(:assert,
                              Exception >= exception_type,
                              "Should expect a class of exception, " +
                              "#{exception_type}")
              exception_classes << exception_type
            end
          end
          [exception_classes, exception_modules, exception_objects]
        end

        def expected_class?(actual_exception, equality)
          @expected_classes.any? do |expected_class|
            actual_exception.send(equality, expected_class)
          end
        end

        def expected_module?(actual_exception)
          @expected_modules.any? do |expected_module|
            actual_exception.is_a?(expected_module)
          end
        end

        def expected_object?(actual_exception)
          @expected_objects.any? do |expected_object|
            expected_object == actual_exception or
              fallback_exception_object_equal(expected_object, actual_exception)
          end
        end

        def fallback_exception_object_equal(expected_object, actual_exception)
          owner = Util::MethodOwnerFinder.find(expected_object, :==)
          if owner == Kernel or owner == Exception
            expected_object.class == actual_exception.class and
              expected_object.message == actual_exception.message
          else
            false
          end
        end
      end

      # :startdoc:
    end
  end
end
