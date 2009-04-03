assert "nil", "p @foo"

assert "42", %q{
  class Foo
    def initialize; @x = 42; end;
    def x; @x; end
  end
  p Foo.new.x
}

assert "42", %q{
  class Foo
    def initialize; @x, @y = 40, 2; end
  end
  class Foo
    def z; @z ||= @x + @y; end
  end
  p Foo.new.z
}

assert "42", %q{
  class Foo
    def initialize; @x = 42; end
  end
  o = Foo.new
  class Foo
    def y; @y ||= @x; end
  end
  p o.y
}

assert "42", %q{
  instance_variable_set(:@foo, 42)
  p instance_variable_get(:@foo)
}

assert "42", %q{
  class Foo
    def initialize; @x = 42; end
  end
  o = Foo.new
  p o.instance_variable_get(:@x)
}

assert "42\n123", %q{
  class Foo
     @foo = 42
     def self.foo; @foo; end
     def self.foo=(x); @foo=x; end
  end
  p Foo.foo
  Foo.foo=123
  p Foo.foo
}

assert '231', %q{
  class Foo
    def initialize
      @v1 = 1;   @v2 = 2;   @v3 = 3
      @v4 = 4;   @v5 = 5;   @v6 = 6
      @v7 = 7;   @v8 = 8;   @v9 = 9
      @v10 = 10; @v11 = 11; @v12 = 12
      @v13 = 13; @v14 = 14; @v15 = 15
      @v16 = 16; @v17 = 17; @v18 = 18
      @v19 = 19; @v20 = 20; @v21 = 21
    end
    def foo
      @v1 + @v2 + @v3 + @v4 + @v5 + @v6 +
      @v7 + @v8 + @v9 + @v10 + @v11 + @v12 +
      @v13 + @v14 + @v15 + @v16 + @v17 + @v18 +
      @v19 + @v20 + @v21
    end
  end
  p Foo.new.foo
}