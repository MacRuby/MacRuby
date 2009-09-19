assert "M", "module M; end; p M"
assert "Module", "module M; end; p M.class"

assert "42", "x = module Foo; 42; end; p x"
assert "nil", "x = module Foo; end; p x" 

assert '"hello world"', %{
  module A; end

  module B
    def greetings
      'hello'
    end
  end     

  module C
    def greetings
      super + ' world'
    end
  end  

  A.extend(B)
  A.extend(C)
  p A.greetings
}

assert '42', %{
  module Foo
    define_method('bar') do
      42
    end
  end
  class A
    include Foo
  end
  p A.new.bar
}

assert ':ok', %{
  module M; end
  begin
    M.new
    p :ko
  rescue NoMethodError
    p :ok
  end
}
