assert "42", "def foo; 42; end; p foo"
assert "42", "def foo(x); x + 41; end; p foo(1)"
assert "42", "def foo(x, y); x + y; end; p foo(40, 2)"
assert "42", "def foo(x, y, z); x + y + z; end; p foo(40, 1, 1)"

assert "42", "def foo(x=42); x; end; p foo"
assert "42", "def foo(x=0); x; end; p foo(42)"
assert "42", "def foo(x=30, y=10, z=2); x+y+z; end; p foo"
assert "42", "def foo(x=123, y=456, z=789); x+y+z; end; p foo(30, 10, 2)"
assert "42", "def foo(x, y=40); x+y; end; p foo(2)"
assert "42", "def foo(x, y=123); x+y; end; p foo(20, 22)"
assert "42", "def foo(x, y, z=2); x+y+z; end; p foo(20, 20)"
assert "42", "def foo(x, y, z=123); x+y+z; end; p foo(20, 20, 2)"
assert "42", "def foo(x, y=20, z=2); x+y+z; end; p foo(20)"
assert "42", "def foo(x, y=123, z=2); x+y+z; end; p foo(30, 10)"

assert "126", "def foo(a=b=c=42); a+b+c; end; p foo"
assert "[42, nil]", "def f(a=X::x=b=1) p [a, b] end; f(42)"

assert "42", "def foo; 1; end; i = 0; while i < 42; i += foo; end; p i"
assert "42", %q{
  class X; def foo; 15; end; end
  class Y; def foo; 01; end; end
  class Z; def foo; 05; end; end
  x = X.new
  y = Y.new
  z = Z.new
  i = 0
  i += x.foo; i += y.foo; i += z.foo
  i += x.foo; i += y.foo; i += z.foo
  p i
}

assert "42", %q{
  class X;     def foo; p 42;  end; end
  class Y < X; def foo; super; end; end
  Y.new.foo
}

assert "42", %q{
  class X;     def foo(x); p x;       end; end
  class Y < X; def foo;    super(42); end; end
  Y.new.foo
}

assert "42", %q{
  class X;     def foo(x); p x;   end; end
  class Y < X; def foo(x); super; end; end
  Y.new.foo(42)
}

assert "42", %q{
  class X;     def foo; 42;    end; end
  class Y < X; def foo; super; end; end
  class Z < Y; def foo; super; end; end
  p Z.new.foo
}

assert "42", "def foo; 42; end; p send(:foo)"
assert "42", "def foo(x, y); x + y; end; p send(:foo, 40, 2)"

assert "42", %q{
  def foo; :nok; end
  def send(x); 42; end
  p send(:foo)
}

assert "42", %q{
  class Object
    def send(x); 42; end
  end
  def foo; :nok; end
  p send(:foo)
}

assert "42", "def foo; return 42; end; p foo"
assert "42", "def foo(x); if x; return 42; end; end; p foo(true)"
assert "42", "def foo(x); if x; x += 2; return x; end; end; p foo(40)"
assert "42", "def foo(x); if x; x += 2; return x; x += 666; end; end; p foo(40)"

assert "[1, 2, 3]", "def foo; return 1, 2, 3; end; p foo"

assert "42", "def foo=(x); @x = x + 1; end; self.foo=41; p @x"
assert "42", "def []=(x, y); @x = x + y; end; self[40]=2; p @x"

assert "[]", "def foo; return *[]; end; p foo"

assert '42', "def foo; 1.times { return 42 }; p :nok; end; p foo"

assert "42", "def foo(x,y,z); x+y+z; end; a=[20,10,12]; p foo(*a)"
assert "42", "def foo(x,y,z); x+y+z; end; a=[20,10]; p foo(*a, 12)"
assert "42", "def foo(x,y,z); x+y+z; end; a=[20,10]; p foo(12, *a)"
assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; p foo(10, 12, *a)"
assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; p foo(10, *a, 12)"
assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; p foo(*a, 10, 12)"
assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; b=[10,12]; p foo(*a, *b)"
assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; b=[10]; p foo(*a, 12, *b)"
assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; b=[10]; p foo(12, *a, *b)"
assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; b=[10]; p foo(*a, *b, 12)"
assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; b=[10]; c=[12]; p foo(*a, *b, *c)"
assert "42", "def foo(x); x; end; a=42; p foo(*a)"

assert ":ok", "def foo(*args); :ok; end; p foo"
assert ":ok", "def foo(&block); :ok; end; p foo"
assert ":ok", "def foo(*args, &block); :ok; end; p foo"
assert ":ok", "def foo(x, *args, &block); x; end; p foo(:ok)"
assert ":ok", "def f(&proc) p :ok; end; f(&nil)"

assert ":ok", %{
  def foo(&block) p(block ? :ko : :ok) end
  def bar(&block) block.call() end
  bar { foo }
}

assert "[1, nil, :c]", "def f(a, b = :b, c = :c) [a, b, c] end; p f(1, nil)"
assert "[1, :b, :c, 2]\n[1, 2, :c, 3]\n[1, 2, 3, 4]", %{
  def f(a, b = :b, c = :c, d) [a, b, c, d] end
  p f(1, 2)
  p f(1, 2, 3)
  p f(1, 2, 3, 4)
}
assert "[1, :b, :c, 2]\n[1, 2, :c, 3]\n[1, 2, 3, 4]", %{
  def f(a, b = :b, c = :c, d) [a, b, c, d] end
  p f(1, 2)
  p f(1, 2, 3)
  p f(1, 2, 3, 4)
}
assert "[1, :b, :c, [], 2, 3]\n[1, 2, :c, [], 3, 4]\n[1, 2, 3, [], 4, 5]\n[1, 2, 3, [4], 5, 6]\n[1, 2, 3, [4, 5], 6, 7]", %{
  def f(a, b = :b, c = :c, *args, d, e) [a, b, c, args, d, e] end
  p f(1, 2, 3)
  p f(1, 2, 3, 4)
  p f(1, 2, 3, 4, 5)
  p f(1, 2, 3, 4, 5, 6)
  p f(1, 2, 3, 4, 5, 6, 7)    
}
assert "42", "def f((a, b)); a end; p f([42, 53])"
assert "42", "def f((a, b)); a end; p f([42, 53, 64])" # ignore additional elements in the array
assert "[42, nil]", "def f((a, b)); [a, b] end; p f([42])" # not used args are set to nil
assert "[1, 2, [], 3, nil]\n[1, 2, [], 3, 4]\n[1, 2, [3], 4, 5]", %{
  def f((x, y, *a, b, c)); [x, y, a, b, c] end
  p f([1, 2, 3])
  p f([1, 2, 3, 4])
  p f([1, 2, 3, 4, 5])
}
assert "true", %{
  class A; def to_ary; [42]; end; end
  def f((*a)); a; end;
  p f(A.new) == [42]
} # to_ary (not to_a) is called on non-Array objects
assert "true", %{def f((*a)); a; end; o = Object.new; p f(o) == [o]} # objects without to_ary are just passed in a one element array

assert ":ok", "def f(x = 1) :ko; end; def f() end; begin p f(1); rescue ArgumentError; p :ok; end"
assert ":ok", "def f(x) :ko; end; def f() end; begin p f(1); rescue ArgumentError; p :ok; end"
assert ":ok", "def f() :ko; end; def f(x) end; begin p f(); rescue ArgumentError; p :ok; end"

assert ":ok", "def f(); end; begin f(1); rescue ArgumentError; p :ok; rescue; p :ko; end"
assert ":ok", "def f(a); end; begin f; rescue ArgumentError; p :ok; rescue; p :ko; end"
assert ":ok", "def f(a); end; begin f(1, 2); rescue ArgumentError; p :ok; rescue; p :ko; end"
assert ':ok', "def f(a, b); end; begin; f; rescue ArgumentError; p :ok; rescue; p :ko; end"
assert ':ok', "def f(a, b); end; begin; f(1, 2, 3); rescue ArgumentError; p :ok; rescue; p :ko; end"

assert ':ok', "def f(a, b); end; begin; a=[1]; f(*a); rescue ArgumentError; p :ok; rescue; p :ko; end"
assert ':ok', "def f(a, b); end; begin; a=[1,2,3]; f(*a); rescue ArgumentError; p :ok; rescue; p :ko; end"

assert ":ok", %{
  def func()
    1.times { |x| func() }
  end
  p :ok
}

assert "1\n2\n2", %q{
  def func
    p 1
    def func
      p 2
    end
    func
  end
  func
  func
}

assert "[1, 2]", %{
  def f
    yield 1, 2
  end
  f {|*args| p args}
}

assert ':ok', %{
  1.times do
    def foo(&a)
      a.call
    end
    foo { p :ok }
  end
}

assert '42', %{
  class Foo
    def self.foo; 42; end
  end
  p Foo.foo
}
assert '42', %{
  class Foo
    class << self
      def foo; 42; end
    end
  end
  p Foo.foo
}
assert '42', %{
  class Foo; end
  def Foo.foo; 42; end
  p Foo.foo
}
assert '42', %{
  o = Object.new
  def o.foo; 42; end
  p o.foo
}
assert '42', %{
  o = Object.new
  class << o
    def foo; 42; end
  end
  p o.foo
}

assert '42', %{
  def foo; p 42; end
  def bar(a = foo); end
  bar
}

assert '42', %{
  def foo() yield 1, 2 end
  x = 1
  w = 42
  foo { |x, y = :y| p w }
}
