assert "X", "class X; end; p X"
assert "Class", "class X; end; p X.class"
assert "true", "class X; end; p X.superclass == Object"
assert "Y", "class X; end; class Y < X; end; p Y"
assert "X", "class X; end; class Y < X; end; p Y.superclass"

assert "42", "class X; def initialize; p 42; end; end; X.new"
assert "42", "class X; def initialize(x); p x; end; end; X.new(42)"

assert "42", "class X; def initialize; yield; end; end; p X.new { break 42 }"

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

assert "42", %{
  class X
    define_method(:foo) { 42 }
  end
  p X.new.foo
}

assert "42", %{
  class Module
    m = method(:method_added)
    define_method(:method_added) do |name|
      p 42 if name == :test
      m.call(name)
    end
  end
  
  class Foo
    def test; end
  end
}

assert "true", %{
  class X
    define_singleton_method(:foo, method(:constants))
  end
  p X.foo == X.constants
}

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
}, :known_bug => true

assert "42", %{
  class X
    def foo
      42
    end
  end
  class Y < X
    def foo
      proc { super }.call
    end
  end
  p Y.new.foo
}

# Mocha TODO
assert "true", %q{
  class X
    class << self
      def foo; end
      p public_method_defined?(:foo)
    end
  end
}

# Mocha TODO
assert "false", %q{
  class X
    class << self
      def foo; end
      remove_method(:foo)
    end
    p respond_to?(:foo)
  end
}

assert ':ok', %{
  class Foo < Mutex
    def initialize
      @foo = :ok
      super
      p @foo
    end
  end
  Foo.new
}


# Test cloning
assert "B, [:CONST_M], [:ok]", %{
  module M
    CONST_M = 1
    def ok; end
  end
  B = M.clone
  puts B.to_s + ", " + B.constants.to_s + ", " + B.instance_methods(false).to_s
}

assert "B, [:CONST_A], [:ok]", %{
  class A
    CONST_A = 1
    def ok; end
  end
  B = A.clone
  puts B.to_s + ", " + B.constants.to_s + ", " + B.instance_methods(false).to_s
}

assert "B, [:CONST_C, :CONST_A], []", %{
  class A
    CONST_A = 1
    def ok; end
  end
  class C < A
    CONST_C = 1
  end
  B = C.clone
  puts B.to_s + ", " + B.constants.to_s + ", " + B.instance_methods(false).to_s
}

# Test dup
assert "B, [:CONST_M], [:ok]", %{
  module M
    CONST_M = 1
    def ok; end
  end
  B = M.dup
  puts B.to_s + ", " + B.constants.to_s + ", " + B.instance_methods(false).to_s
}

assert "B, [:CONST_A], [:ok]", %{
  class A
    CONST_A = 1
    def ok; end
  end
  B = A.dup
  puts B.to_s + ", " + B.constants.to_s + ", " + B.instance_methods(false).to_s
}

assert "B, [:CONST_C, :CONST_A], []", %{
  class A
    CONST_A = 1
    def ok; end
  end
  class C < A
    CONST_C = 1
  end
  B = C.dup
  puts B.to_s + ", " + B.constants.to_s + ", " + B.instance_methods(false).to_s
}

# should obviously be a real file path when run from a file...
assert %{["-:7:in `<main>'"]}, %{
  class X
    def self.inherited(klass)
      p caller
    end
  end
  class Y < X; end
}, :known_bug => true

assert '[2, 3, 4, 5]', %{
  class Foo
    define_method(:foo) {|_, *a| p a }
  end
  Foo.new.foo(1,2,3,4,5)
}

assert '[2, 3, 4]', %{
  class Foo
    define_method(:foo) { |a, *b, c| p b }
  end
  Foo.new.foo(1,2,3,4,5)
}
