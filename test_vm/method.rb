assert ":ok", %{
  def foo; :ok; end
  p method(:foo).call
}

assert "42", %{
  def foo(x); x; end
  p method(:foo).call(42)
}

assert ":ok", %{
  begin
    method(:does_not_exist)
  rescue NameError
    p :ok
  end
}

assert ":b\n:a", %{
  class A; def foo() :a end end
  class B < A; def foo() :b end end
  m = A.instance_method(:foo)
  b = B.new
  p b.foo, m.bind(b).call
}

assert '-5', "def f(a, b, d, g, c=1, e=2, f=3, *args); end; p method(:f).arity"
assert '-5', "def f(a, b, d, g, c=1, e=2, f=3); end; p method(:f).arity"
assert '-5', "def f(a, b, d, g, *args); end; p method(:f).arity"
assert '4', "def f(a, b, d, g); end; p method(:f).arity"

assert ":ok", %{
  def foo; p :ok; end
  m = method(:foo)
  def foo; p :nok; end
  m.call 
}

assert "42", %{
  o = 42
  m = o.method(:description)
  puts m.call
}

assert ":ok", %{
  o = {}
  k = 'omg'
  m = o.method(:"setObject:forKey:")
  m.call(:ok, k)
  p o[k]
}

assert '42', %{
  def foo=(x); x+=1; end
  p(self.foo=42)
}

assert '42', %{
  def foo; p 42; end
  foo(*[])
}

assert '42', %{
  def f(args) p args[:in] end
  f(in: 42)
}

assert ':ok', %{
  class HasNoInit; end
  class C < HasNoInit; end
  class D < C
    def init
      super
      self
    end
  end
  D.new
  p :ok
}

assert 'true', %{
  class X
    def foo; end
    p method_defined?(:foo)
  end
}

assert '0', %{
  class X; def self.foo(x); x * 2; end; end
  X.method(:foo).call(21)
  p $SAFE
}

assert '0', %{
  class X; def foo(x); x * 2; end; end
  X.new.method(:foo).call(21)
  p $SAFE
}

assert '42', %{
  def foo(&b); b.call; end
  method(:foo).call(&proc{p 42})
}

assert '42', %{
  def foo(&b); p 42 if b.nil?; end
  method(:foo).to_proc.call(&proc{})
}
