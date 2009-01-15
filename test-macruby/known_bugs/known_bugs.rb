#!/usr/local/bin/macruby

require "test/unit"
#framework 'Cocoa'

module KnownBugs
  class TestBugsInC < Test::Unit::TestCase 
    def setup
      dir = File.expand_path('../', __FILE__)
      exit(1) unless system("cd #{dir} && macruby extconf.rb && make")
      require File.join(dir, 'known_bugs_in_c')
    end

    def test_accessing_an_array_created_via_rb_str_split2
      KnownBugsInC.test_rb_str_split2("foo:bar", ":")
    end
  end

  class TestKernel < Test::Unit::TestCase
    module ::Kernel
      private
      def is_callable?; true end
    end

    module ::Kernel
      def should_be_callable?; true end
      private :should_be_callable?
    end

    def test_kernel_methods_made_private_with_keyword
      assert is_callable? # works
    end

    def test_kernel_methods_made_private_with_class_method
      assert should_be_callable? # causes endless loop
    end
  end

  class TestDuplicatingInstances < Test::Unit::TestCase
    # Works

    class Foo; end

    def test_dup_on_an_instance_of_a_pure_ruby_class
      obj = Foo.new
      assert_not_equal obj, obj.dup.object_id
    end

    # Fails

    def test_dup_on_an_instance_of_Object
      obj = Object.new
      assert_nothing_raised(NSException) do
        # Raises: [NSObject copyWithZone:]: unrecognized selector sent to instance
        assert_not_equal obj.object_id, obj.dup.object_id
      end
    end

    def test_dup_on_a_class_instance
      assert_not_equal Foo.object_id, Foo.dup.object_id
    end
  end

  class TestStringFormatting < Test::Unit::TestCase
    def test_formatting_with_a_Bignum
      assert_nothing_raised(RangeError) { "%d" % 68727360256 }
    end
  end

  class TestIncludingModuleInClass < Test::Unit::TestCase
    module ClassInstanceMethod
      def a_class_instance_method; end
    end

    class ::Class
      include ClassInstanceMethod
    end

    def test_class_should_respond_to_methods_included_in_Class
      assert Class.new.respond_to?(:a_class_instance_method)
    end
  end

  class TestIncludingModuleInModule < Test::Unit::TestCase
    module ModuleInstanceMethod
      def a_module_instance_method; end
    end

    class ::Module
      include ModuleInstanceMethod
    end

    def test_module_should_respond_to_methods_included_in_Module
      assert Module.new.respond_to?(:a_module_instance_method)
    end
  end

  class TestConstantLookup < Test::Unit::TestCase
    module Namespace
      NamespacedConstant = nil
      class NamespacedClass; end
    end

    def test_should_not_find_namespaced_constants # works
      assert_raise(NameError) { NamespacedConstant }
    end

    def test_should_not_find_namespaced_classes # fails
      assert_raise(NameError) { NamespacedClass }
    end
  end

  class TestRespondTo < Test::Unit::TestCase
    class RespondTo
      def respond_to?(method, hidden = false)
        super
      end
    end

    def test_super_implementation
      assert_nothing_raised(SystemStackError) do
        RespondTo.new.respond_to?(:object_id)
      end
    end
  end

  class TestBooleanComparison < Test::Unit::TestCase
    def test_NSCFBoolean_comparison_to_Ruby_bool
      assert_equal true,  NSNumber.numberWithBool(true)
      assert_equal false, NSNumber.numberWithBool(false)
    end
  end
end