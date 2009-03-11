require "test/unit"

# The following are just some simple hacks to be able to write tests with a
# spec like description. This should ease the process of moving tests to run
# with mspec for instance.

module Test::Unit
  class TestCase
    class << self
      # Runs before each test case.
      def before(&block)
        define_method("setup", &block)
      end
      
      # Runs after each test case.
      def after(&block)
        define_method("teardown", &block)
      end
      
      # Defines a test case.
      def it(name, &block)
        define_method("test_#{name}", &block)
      end
    end
  end
  
  class Failure
    # Return the failure message in a more spec like way.
    def long_display
      file_location, method_name = @location.first.split(':in `')
      "#{@test_name} `#{method_name.sub(/^test_/, '')} [#{file_location}]\n#{@message}"
    end
  end
end

module Kernel
  private
  
  # Defines a new test case with the given +description+ and optionally a test
  # case superclass.
  #
  #   describe "Foo in general" do
  #     it "should instantiate" do
  #       assert Foo.new.nil?
  #     end
  #   end
  def describe(description, superclass = Test::Unit::TestCase, &definition)
    klass = Class.new(superclass, &definition)
    klass.class_eval "def name; '#{description}' end"
    klass
  end
end