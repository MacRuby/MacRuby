#!/usr/local/bin/macruby

require "test/unit"

module KnownBugs
  class TestYaml < Test::Unit::TestCase
    require "yaml"
    class IDontWantToCrash; end
    
    def test_load_non_native_classes
      data = YAML.dump(IDontWantToCrash.new)
      assert_nothing_raised { YAML.load(data) }
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
end