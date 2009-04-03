assert '42', %q{
  class Foo; attr_accessor :foo; end
  o = Foo.new
  o.foo = 42
  p o.foo
}

assert '42', %q{
  class Foo
    attr_reader :foo
    attr_writer :foo
  end
  o = Foo.new
  o.foo = 42
  p o.foo
}

assert 'nil', "class Foo; attr_reader :foo; end; p Foo.new.foo"

assert ':ok', %q{
  class Foo; attr_reader :foo; end
  o = Foo.new
  begin
    o.foo = 42
  rescue NoMethodError
    p :ok
  end
}

assert ':ok', %q{
  class Foo; attr_writer :foo; end
  o = Foo.new
  begin
    o.foo
  rescue NoMethodError
    p :ok
  end
}

assert '42', %q{
  class Foo
    attr_writer :foo
    def bar; @foo; end
  end
  o = Foo.new
  o.foo = 42
  p o.bar
}
