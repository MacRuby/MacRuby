assert "42", "p eval('40 + 2')"
assert "42", "def foo; 42; end; p eval('foo')"
assert "42", "x = 40; p eval('x + 2')"
assert "42", "x = 0; eval('x = 42'); p x"

assert ":ok", "eval('x = 42'); begin; p x; rescue NameError; p :ok; end"

assert "42", %q{
  def foo(b); x = 42; eval('x', b); end
  p foo(nil)
}

assert "42", %q{
  def foo(b); x = 43; eval('x', b); end
  x = 42
  p foo(binding)
}

assert "42", %q{
  def foo(b); x = 0; eval('x = 42', b); end
  x = 1
  foo(binding)
  p x
}

assert "42", %q{
  def foo; x = 123; bar {}; x; end
  def bar(&b); eval('x = 42', b.binding); end
  p foo
}

assert '42', %q{
  def foo; x = 42; proc {}; end
  p = foo; eval('p x', p.binding)
}

assert "42", %q{
  class Foo;
    def foo; 42; end;
    def bar(s, b); eval(s, b); end;
  end
  def foo; 123; end
  Foo.new.bar('p foo', nil)
}

assert "42", %q{
  class Foo;
    def foo; 43; end;
    def bar(s, b); eval(s, b); end;
  end
  def foo; 42; end
  Foo.new.bar('p foo', binding)
}

assert "42", "class A; def foo; @x; end; end; x = A.new; x.instance_eval { @x = 42 }; p x.foo"

assert "42", "b = binding; eval('x = 42', b); eval('p x', b)"

assert ":ok", "module M; module_eval 'p :ok'; end"
assert ":ok", "module M; module_eval 'def self.foo; :ok; end'; end; p M.foo"

assert '42', %{
  $b = proc do p X end
  module A
   X = 42
   module_eval &$b
  end
}

assert '42', %{
  module A
    X = 42
    class B
      def foo(&b)
        (class << self; self; end).module_eval &b
      end
    end
  end
  A::B.new.foo do p X end
}
