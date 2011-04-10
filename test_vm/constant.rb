assert '42', "FOO=42; p FOO"
assert '42', "FOO=42; p Object::FOO"
assert '42', "class X; FOO=42; end; p X::FOO"
assert '42', "class X; end; X::FOO=42; p X::FOO"

assert ':ok', %q{
  class X; FOO=123; end
  begin
    p FOO
  rescue NameError
    p :ok
  end
}

assert '42', "class X; FOO=42; def foo; FOO; end; end; p X.new.foo"
assert '42', %q{
  FOO=123
  class X; FOO=42; end
  class Y < X; def foo; FOO; end; end
  p Y.new.foo
}

assert '42', "FOO=42; 1.times { p FOO }"
assert '42', %q{
  module X
    FOO=42
    1.times { p FOO }
  end
}

assert '42', %q{
  class X
    FOO = 42
    class Y; def foo; FOO; end; end
  end
  p X::Y.new.foo
}

assert '42', %q{
  class X; FOO = 42; end
  class Y < X; def foo; FOO; end; end
  p Y.new.foo
}

assert '42', %q{
  class X; FOO = 123; end
  class Z
    FOO = 42
    class Y < X; def foo; FOO; end; end
  end
  p Z::Y.new.foo
}

assert '42', %q{
  class A
    module B
      def foo
        42
      end
    end
    module C
      extend B
    end
  end
  p A::C.foo
}

assert 'true', 'p ::String == String'

assert '42', %q{
  o = Object.new
  class << o
    module Foo
      Bar = 42
    end
    class Baz; include Foo; end
    class Baz;
      def self.bar; Bar; end
    end
    def baz; Baz; end
  end
  p o.baz.bar
}, :known_bug => true

assert '42', %q{
  module M
    FOO = 42
    class X
      class << self; p FOO; end
    end
  end
}

assert '1', "class Float; class X; p ROUNDS; end; end"

assert '42', %{
  class A
    B = 42
  end
  A.class_eval {
    def bar
      p B
    end
  }
  A.new.bar
}, :known_bug => true

assert ':ok', %{
  module M
    FOO=42
  end
  class M::Foo
    begin
      p FOO
    rescue NameError
      p :ok
    end
  end
}

assert '42', %{
  class Foo
    FOO=42
    def hey
      Object.new.instance_eval { p FOO }
    end
  end
  Foo.new.hey
}, :known_bug => true

assert '42', %{
  module Foo; end
  module Foo::Bar
    FOO=42
    class X
      p FOO
    end
  end
}

assert '42', %{
  module M
    FOO=42
    class C; end
    class C::C2
      p FOO
    end
  end
}

assert '42', %{
  module M
    FOO=42
  end
  class M::C; end
  module M
    class C
      def initialize
        p FOO
      end
    end
  end
  M::C.new
}
