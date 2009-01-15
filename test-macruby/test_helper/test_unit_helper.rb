# The following is just a simple hack to be able to write tests with a spec description.
# This should ease the process of moving tests to run with mspec for instance.
class Test::Unit::TestCase
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
  
  private
  
  # Returns the path to a file in +FIXTURE_PATH+.
  def fixture(name)
    File.join(FIXTURE_PATH, name)
  end
end