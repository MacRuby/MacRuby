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

assert ':ok', %{
  $b = proc do p X end
  module A
    X = :ko
    begin
      module_eval &$b
    rescue NameError
      p :ok
    end
  end
}, :known_bug => true

assert ':ok', %{
  module A
    X = :ko
    class B
      def foo(&b)
        (class << self; self; end).module_eval &b
      end
    end
  end
  begin
    A::B.new.foo do p X end
  rescue NameError
    p :ok
  end
}, :known_bug => true

assert '42', %{
  class A; end
  A.class_eval %{
   def foo
     baz = {}
     bar = baz[:foo] = 42
     bar
   end
  }
  p A.new.foo  
}

assert '42', 'a = nil; 1.times { a = 42; eval "p a" }'

assert 'main', "p eval('self')"
assert 'main', "p eval('self', binding)"
assert 'main', "p eval('self', proc{}.binding)"

assert '42', %{
  b = nil
  1.times {
    a = 42
    b = binding
  }
  eval "p a", b
}

assert '42', 'y = eval("proc {|x| p x}"); y.call(42)'

assert '42', 'eval("def test; a = 42; p a; end; test")'
assert '42', 'eval("a = 42; def test; a = 0; end; test; p a", TOPLEVEL_BINDING)'
assert '42', 'eval("a = 42; def test; a = 0; end; test; p a")'

assert 'true', %{
  begin
    eval("1 = 1")
  rescue Exception => e
  end
  p e.backtrace.kind_of? Array
}

assert 'true', %{
  class Foo
    def get_binding
      binding
    end
  end
  b = Foo.new.get_binding { 42 }
  p eval "block_given?", b
}

assert '42', %{
  class Foo
    def get_binding
      binding
    end
  end
  b = Foo.new.get_binding { 42 }
  p eval "yield", b
}

assert '42', %{
  class Foo
    FOO=42
    class_eval("def foo; FOO; end")
  end
  p Foo.new.foo
}
