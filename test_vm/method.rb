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
