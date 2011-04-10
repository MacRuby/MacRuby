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
assert "42", "def foo; :ko; end; begin; p self.foo; rescue NoMethodError; p 42 end"

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

assert "true", %{
  class X;   def foo(x); x;                        end; end
  module M1; def foo(x); super(true);              end; end
  module M2; def foo(x); return false if x; super; end; end
  class Y < X; include M1; include M2; def foo; super(false); end; end
  p Y.new.foo
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

assert ":ok", %{
  begin
    send(:does_not_exist)
  rescue NoMethodError
    p :ok
  end
}

assert ':ok', %{
  class X
    def method_missing(x, *args, &block)
      p :ko
    end
    
    protected
    def foo
      p :ok if yield == 42
    end
  end
  X.new.send(:foo) { 42 }
}

assert "42", "def foo; return 42; end; p foo"
assert "42", "def foo(x); if x; return 42; end; end; p foo(true)"
assert "42", "def foo(x); if x; x += 2; return x; end; end; p foo(40)"
assert "42", "def foo(x); if x; x += 2; return x; x += 666; end; end; p foo(40)"

assert "[1, 2, 3]", "def foo; return 1, 2, 3; end; p foo"

assert "42", "def foo=(x); @x = x + 1; end; self.foo=41; p @x"
assert "42", "def []=(x, y); @x = x + y; end; self[40]=2; p @x", :known_bug => true

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
assert ":ok", "def f(&proc) p :ok; end; f(&nil)", :known_bug => true

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
}, :known_bug => true

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

# Tail-call elimination
assert '42', %{
  def foo(n); return if n == 0; foo(n-1); end
  foo(30000000)
  p 42
}, :known_bug => true

assert 'true', 'p Object.new.methods.include?(:object_id)'
assert "false\ntrue", %{
  obj = Object.new
  meta = class << obj; self; end
  p obj.respond_to?(:bar) # without this call to respond_to it works fine
  meta.class_eval { def bar; end }
  p obj.respond_to?(:bar)
}

assert '42', %{
  module M; def foo; 42; end; end
  module M2; include M; end
  class X; include M2; end
  p X.new.foo
}

assert '42', %{
  module M; def foo; 42; end; end
  module M2; def bar; foo; end; include M; end
  class X; include M2; end
  p X.new.bar
}

assert '42', %{
  module M
    def foo; 123; end
    module_function :foo
  end
  class X; def foo; 42; end; end
  include M
  p X.new.foo
}

assert "42\n42\n42\n42", %{
  module M
    def foo(*args); 42; end
    module_function :foo

    module_function
    def bar(*args); 42; end
  end

  p M.foo
  p M.foo('blah')
  p M.bar
  p M.bar('blah')
}

assert '42', %{
  def foo(x,y,z=42)
    return z if z == 42
    foo(1,2)
  end
  p foo(1,2,3)
}

assert ':ok', %{
  class X
    def method_missing(x, *args)
      p :ok if x == :foo
    end
  end
  X.new.foo
}

assert ':ok', %{
  class X
    def method_missing(x, *args)
      p :ok if x == :foo and args == [42]
    end
  end
  X.new.foo(42)
}

assert ':ok', %{
  class X
    def method_missing(x, *args, &block)
      p :ok if x == :foo and block.call == 42
    end
  end
  X.new.foo { 42 }
}

assert ':ok', %{
  class X
    def method_missing(x, *args, &block)
      p :ok if x == :foo and block.call == 42
    end
    
    private
    def foo; end
  end
  X.new.foo { 42 }
}

assert ':ok', %{
  module M
    def initialize(*args)
      super
    end
  end
  class X
    include M
  end
  X.new
  p :ok
}

assert '42', %{
  class X
    def foo(x); p x; end
  end
  class Y < X
    def foo(*args); super; end
  end
  Y.new.foo(42)
}

assert %{"hello world"\n42}, %{
  class X; def foo(x,y); p x,y; end; end
  class Y < X; def foo(x,*args); x << ' world'; args[0] = 42; super; end; end
  Y.new.foo('hello', 1)
}

assert %{"hello world"\n42}, %{
  class X; def foo(x,y); p x,y; end; end
  class Y < X; def foo(x,*args); x << ' world'; args = 42; super; end; end
  Y.new.foo('hello', 1)
}

assert 'true', %{
  class X; def foo; p block_given?; end; end
  class Y < X; def foo(&b); super; end; end
  Y.new.foo {}
}

assert "42", %{
  class X; def foo(x); p x; end; end
  class Y < X; def foo(x); 1.times { |; x| x = 1; super; } end; end
  Y.new.foo(42)
}, :known_bug => true

assert "42", %{
  module M
    def foo; p 42; end
  end
  class Module
    include M
  end
  class Range; foo; end
}, :known_bug => true

assert ':ok', %{
  class B; end
  class A < B
    def initialize
      super(nil)
    end
  end

  begin
    A.new
    p :ok
  rescue ArgumentError
    p :ko
  end
}, :known_bug => true

assert '42', %{
  class C1
    def foo(x); p x; end
  end
  class C2 < C1
    def foo(x=42); super; end
  end
  C2.new.foo
}

assert ':ok', %{
  class C1
    def foo; p :ok; end
  end
  class C2 < C1
    def foo; 1.times { return super }; end
  end
  C2.new.foo
}

assert ':ok', %{
  begin
    super
  rescue NoMethodError
    p :ok
  end
}

assert ':ok', %{
  begin
    1.times { super }
  rescue NoMethodError
    p :ok
  end
}

assert "true", %{
  class Foo
    def respond_to?(*x); super; end
  end
  p Foo.new.respond_to?(:object_id)
}

assert ":ok", %{
  class C1
    def foo(arg); end
  end
  class C2 < C1
    def foo; super; end
  end
  begin
    C2.new.foo
  rescue ArgumentError
    p :ok
  end
}, :known_bug => true

# TODO: find a better place for this.
assert '', %{
  $SAFE=4
  s="omg"
  s.freeze
}

assert ":X2_foo\n:X_foo\n:Y2_foo\n:Y_foo\n:Y2_foo\n:Y_foo\n:Y2_foo\n:Y_foo\n:X2_foo\n:X_foo\n:Y2_foo\n:Y_foo\n:Y2_foo\n:Y_foo\n:Y2_foo\n:Y_foo", %{
  class X
    def initialize
      @children = []
      3.times { @children << Y::Y2.new }    
    end
    def foo
      p :X_foo
      @children.each { |o| o.foo }
    end
    class X2 < X
      def foo
        p :X2_foo
        super
      end
    end
  end
  class Y
    def foo
      p :Y_foo
    end
    class Y2 < Y
      def foo
        p :Y2_foo
        super
      end
    end
  end
  o = X::X2.new
  o.foo
  o.foo
}
