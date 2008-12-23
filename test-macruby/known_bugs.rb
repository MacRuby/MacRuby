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
end