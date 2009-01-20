require 'test/unit'

class Test::Unit::TestCase

  class << self
    def it(name, &block)
      define_method("test_#{name}", &block)
    end
  end

end