assert "X", "class X; end; p X"
assert "Class", "class X; end; p X.class"
assert "true", "class X; end; p X.superclass == Object"
assert "Y", "class X; end; class Y < X; end; p Y"
assert "X", "class X; end; class Y < X; end; p Y.superclass"

assert "42", "class X; def initialize; p 42; end; end; X.new"
assert "42", "class X; def initialize(x); p x; end; end; X.new(42)"

assert "42", %q{
  class X
  end
  o = X.new
  class X
    def foo; p 42; end
  end
  o.foo
}

assert "42", %q{
  class X
    class << self
      def foo; 42; end
    end
  end
  p X.foo
}

assert "42", "x = class Foo; 42; end; p x"
assert "nil", "x = class Foo; end; p x"

assert "42", %q{
  class X
    def foo
      42
    end
  end
  class Y < X
    define_method(:foo) do
      super()
    end
  end
  p Y.new.foo
}