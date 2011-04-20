assert "M", "module M; end; p M"
assert "Module", "module M; end; p M.class"

assert "42", "x = module Foo; 42; end; p x"
assert "nil", "x = module Foo; end; p x" 

assert '"hello world"', %{
  module A; end

  module B
    def greetings
      'hello'
    end
  end     

  module C
    def greetings
      super + ' world'
    end
  end  

  A.extend(B)
  A.extend(C)
  p A.greetings
}

assert '42', %{
  module Foo
    define_method('bar') do
      42
    end
  end
  class A
    include Foo
  end
  p A.new.bar
}

assert ':ok', %{
  module M; end
  begin
    M.new
    p :ko
  rescue NoMethodError
    p :ok
  end
}, :known_bug => true

assert ':ok', %{
  M = Module.new
  begin
    M.new
    p :ko
  rescue NoMethodError
    p :ok
  end
}, :known_bug => true

assert ':ok', %{
  module Bar
    module Baz
      def baz
        p :nok
      end
    end
  end
  class Foo
    def self.baz
      p :ok
    end
  end
  Foo.extend(Bar::Baz)
  Foo.baz
}

assert ':ok', %{
  module Bar
    def baz
      p :nok
    end
  end
  class Foo
    def self.baz
      p :ok
    end
  end
  Foo.extend(Bar)
  Foo.baz
}

assert ':ok', %{
  module Y
    def foo
      p :ok
    end
  end
  module X
    include Y
  end
  o = Object.new
  o.extend X
  o.foo
}

assert ':ok', %{
  module M
    def initialize; p :ok; end
  end
  class X; include M; end
  X.new
}

assert ':ok', %{
  module M
    def foo
      p :ok
    end
  end
  class Class; include M; end
  class X; end
  X.foo
}

assert ':ok', %{
  module M
    def foo
      p :ok
    end
  end
  class Module; include M; end
  module X; end
  X.foo
}

assert ':ok', %{
  module Mok
    def foo
      p :ok
    end
  end
  class Module; include Mok; end
  
  module Mko
    def foo
      p :ko
    end
  end
  class Class; include Mko; end
  
  module X; end
  X.foo
}, :known_bug => true

assert ':ok', %{
  module Mok
    def foo
      p :ok
    end
  end
  class Class; include Mok; end
  
  module Mko
    def foo
      p :ko
    end
  end
  class Module; include Mko; end
  
  class X; end
  X.foo
}, :known_bug => true

assert ':ok', %{
  module M
    def include(*mod)
      @mods ||= []
      @mods.push(*mod)
    end
    def new(*args,&b)
      obj = allocate
      if @mods
        @mods.each { |m| obj.extend(m) }
      end
      obj.send(:initialize,*args,&b)
      obj
    end
  end
  module M2
    def foo; p :ok; end
  end
  class X
    extend M
    include M2
    def foo; raise; end
  end
  X.new.foo
}

assert ':ok', %{
  module M
    def self.foo(v)
      Module.new do
        define_method(:inherited) do |klass|
          super(klass)
          p v
        end
      end
    end
  end
  class X
    extend M.foo(:ok)
  end
  class Y<X
  end
}

assert ':ok', %{
  m = Module.new do
    define_method(:foo) { p :ok }
  end
  Class.new.extend(m).foo
}
