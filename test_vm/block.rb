assert ":ok", "1.times { p :ok }"
assert "42",  "p 42.times {}"
assert "42",  "i = 0; 42.times { i += 1 }; p i"
assert "42",  "x = nil; 43.times { |i| x = i }; p x"
assert "",    "0.times { p :nok }"

assert ":ok", "def foo; yield; end; foo { p :ok }"
assert "42",  "def foo; yield 42; end; foo { |x| p x }"
assert "",    "def foo; end; foo { p :nok }" 

assert ":ok", "def foo; yield; end; send(:foo) { p :ok }"
assert ":ok", "def foo; yield; end; send(:foo, &proc { p :ok })"

assert ":ok", %{
  class X; def initialize; yield ;end; end
  X.new { p :ok }
}

assert ":ok", %{
  def foo; raise; end
  def bar; p :ok unless block_given?; end
  begin
    foo { p :nok }
  rescue
    bar
  end
}

assert ":ok", %{
  def foo; yield; end
  begin
    foo { raise }
  rescue
    1.times { p :ok }
  end
}

assert ":ok", "def foo(&b); b.call; end; foo { p :ok }"
assert "42",  "def foo(&b); b.call(42); end; foo { |x| p x }"
assert ":ok", "def foo(&b); p :ok if b.nil?; end; foo"

assert ":ok", %{
  class X; def initialize(&b); b.call ;end; end
  X.new { p :ok }
}

assert "42", %q{
  def foo; yield 20, 1, 20, 1; end
  foo do |a, b, c, d|
    x = a + b + c + d
    p x
  end
}
assert ":ok", %q{
  def foo; yield; end
  foo do |x, y, z| 
    p :ok if x == nil and y == nil and z == nil
  end
}
assert ":ok", %q{
  def foo; yield(1, 2); end
  foo do |x, y, z| 
    p :ok if x == 1 and y == 2 and z == nil
  end
}
assert ":ok", %q{
  def foo; yield(1, 2); end
  foo do |x| 
    p :ok if x == 1
  end
}
assert ":ok", %q{
  def foo; yield(1, 2); end
  foo do |x, y = :y, z|
    p :ok if x == 1 and y == :y and z == 2
  end
}
assert ":ok", %q{
  def foo; yield(1); end
  foo do |x, y = :y, z|
    p :ok if x == 1 and y == :y and z == nil
  end
}
assert ":ok", %q{
  def foo; yield(1, 2, 3, 4); end
  foo do |x, y = :y, *rest, z|
    p :ok if x == 1 and y == 2 and rest == [3] and z == 4
  end
}
assert ":ok", %q{
  def foo; yield([1, 2]); end
  foo do |x, y = :y, z|
    p :ok if x == 1 and y == :y and z == 2
  end
}
assert "[1, 2]", %q{
  def foo; yield(1, 2); end
  foo { |*rest| p rest }
}
assert "[[1, 2]]", %q{
  def foo; yield([1, 2]); end
  foo { |*rest| p rest }
}
assert "[1, [2]]", %q{
  def foo; yield([1, 2]); end
  foo { |a, *rest| p [a, rest] }
}
assert "[[1, 2], []]", %q{
  def foo; yield([1, 2]); end
  foo { |a = 42, *rest| p [a, rest] }
}
assert "[1, 2, []]", %q{
  def foo; yield([1, 2]); end
  foo { |a = 42, *rest, b| p [a, b, rest] }
}
assert "[1, 2, []]", %q{
  def foo; yield([1, 2]); end
  foo { |a, b = 42, *rest| p [a, b, rest] }
}
assert "[[1, 2], 42, []]", %q{
  def foo; yield([1, 2]); end
  foo { |a = 42, b = 42, *rest| p [a, b, rest] }
}
assert "[[1, 2], []]", %q{
  def foo; yield([1, 2]); end
  foo { |a = 42, *rest| p [a, rest] }
}

assert 'nil', 'p = proc { |x,| p x }; p.call'
assert '42', 'p = proc { |x,| p x }; p.call(42)'
assert '42', 'p = proc { |x,| p x }; p.call(42,1,2,3)'
assert '42', 'p = proc { |x,| p x }; p.call([42])'
assert '42', 'p = proc { |x,| p x }; p.call([42,1,2,3])'

assert "true", "def foo; p block_given?; end; foo {}"
assert "false", "def foo; p block_given?; end; foo"
assert "false", "def foo; p block_given?; end; def bar; foo; end; bar {}"

assert ':ok', "def foo; yield; end; begin; foo; rescue LocalJumpError; p :ok; end"

assert ":ok", "def foo(&m); m.call; end; foo { p :ok }"
assert ":ok", "def foo(&m); p :ok if m == nil; end; foo"

assert "[[1, 0, 1, 0, 1], [0, 1, 1, 0, 0], [0, 0, 0, 1, 1], [0, 0, 0, 0, 0]]", %q{
  def trans(xs)
    (0..xs[0].size - 1).collect do |i|
      xs.collect{ |x| x[i] }
    end
  end
  p trans([1,2,3,4,5])
}, :archs => ['i386']

assert "[[1, 0, 1, 0, 1], [0, 1, 1, 0, 0], [0, 0, 0, 1, 1], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0]]", %q{
  def trans(xs)
    (0..xs[0].size - 1).collect do |i|
      xs.collect{ |x| x[i] }
    end
  end
  p trans([1,2,3,4,5])
}, :archs => ['x86_64']

assert '45', "p (5..10).inject {|sum, n| sum + n }"
assert '151200', "p (5..10).inject(1) {|product, n| product * n }" 

assert "42", "def foo(x); yield x; end; p = proc { |x| p x }; foo(42, &p)"
assert "42", %q{
  class X
    def to_proc; proc { |x| p x }; end
  end
  def foo(x); yield x; end
  foo(42, &X.new)
}
assert "42", "def foo; yield; end; begin; foo(&Object.new); rescue TypeError; p 42; end"

assert "42", "x = 0; proc { x = 42 }.call; p x"
assert "42", "x = 0; p = proc { x += 40 }; x = 2; p.call; p x"

assert "42", "n = 0; 100.times { |i| n += 1; break if n == 42 }; p n"
assert "42", "n = 0; 100.times { |i| next if i % 2 == 0; n += 1; }; p n - 8"
assert "42", "p 100.times { break 42 }"
assert "42", "p proc { next 42 }.call"
assert "42", "i=0; while i<1; begin; i=2; next; ensure; p 42; end; end"

assert "42", "begin p proc { break 24 }.call rescue LocalJumpError; p 42 end"
assert "42", "def foo; yield; end; foo { break }; 1.times {p 42}"
assert "42", "1.times { begin; break; ensure; p 42; end }"
assert "42", "i=0; while i<1; begin; break; ensure; p 42; end; end"

assert "42\n42", "i=0; while true; begin; break if i>0; i=1; redo; ensure; p 42; end; end"

assert "42", "p [42].map { |x| x }.map { |y| y }[0]"

assert '1302', %q{
  $count = 0
  def foo(v, x)
    x.times {
      x -= 1
      foo(v, x)
      $count += v
    }
  end
  foo(42, 5)
  p $count
}

assert '42', "x=42; 1.times { 1.times { 1.times { p x } } }"
assert '42', "def f; 1.times { yield 42 }; end; f {|x| p x}"

assert '42', "def foo; x = 42; proc { x }; end; p foo.call"
assert '42', %q{
  def foo() x=1; [proc { x }, proc {|z| x = z}]; end
  a, b = foo
  b.call(42)
  p a.call
}

assert "2\n1", %{
  def f(x, y)
    1.times {
      f(2, false) if y
      p x
    }
  end
  f(1, true)
}

assert "42", %{
  def f()
    a = nil
    1.times do
      x = 42
      a = proc { x }
    end
    a
  end
  p f.call
}

assert "42", %{
  def f()
    x = 42
    a = nil
    1.times do
      a = proc { x }
    end
    a
  end
  p f.call
}

assert "42", %{
  def f()
    x = 40
    a = nil
    1.times do
      1.times do
        y = 2
        a = proc { x + y }
      end
    end
    a
  end
  p f.call
}

assert "42", %{
  def f()
    a = nil
    b = proc do
      x = 42
      a = proc { x }
    end
    b.call
  end
  p f.call
}

assert "42", %{
  def f()
    x = 42
    a = nil
    b = proc do
      a = proc { x }
    end
    b.call
  end
  p f.call
}

assert "42", %{
  def f()
    a = nil
    b = false
    x = 42
    while true
      return a if b
      b = true
      a = proc { x }
    end
  end
  p f.call
}

assert "42", %{
  def f()
    x = 42
    $a = proc { x }
    raise 'w'
  end
  begin
    f
  rescue
    p $a.call
  end
}

assert "42", %{
  def g()
    raise 'w'
  end
  def f()
    x = 42
    $a = proc { x }
    g
  end
  begin
    f
  rescue
    p $a.call
  end
}

assert "42", %{
  def f()
    x = 42
    $a = proc { x }
    throw :w
  end
  catch(:w) do
    f
  end
  p $a.call
}

assert "42", %{
  def g()
    throw :w
  end
  def f()
    x = 42
    $a = proc { x }
    g()
  end
  catch(:w) do
    f
  end
  p $a.call
}

assert "42", %{
  def f()
    x = 42
    a = proc { x }
    1.times { return a }
  end
  p f.call
}

assert ":ok", %{
  def f()
    2.times do
      begin
        yield
      rescue
      end
    end
  end
  f { raise 'a' }
  p :ok
}

assert ":ok", %{
  def f()
    2.times do
      catch(:a) do
        yield
      end
    end
  end
  f { throw :a }
  p :ok
}

assert ':ok', %{
  def f()
    a = :ok
    b = true
    x = proc { a }
    1.times {
      return x
    }
  end
  p f.call
}

assert ':ok', %{
  def f()
    a = :ok
    b = false
    while true
      return x = proc { a } if b
      b = true
    end
  end
  p f.call
}

assert ':ok', %{
  $a = []
  
  def foo(what, &block)
    $a << block
  end
  
  [:ok].each do |type|
    foo type do
      p type
    end
  end
  
  $a.each do |b| b.call end
}

# Enumerator 
assert "[\"f\", \"o\", \"o\"]", "p 'foo'.chars.to_a"

assert ':ok', %{
  def foo(x); p :ok if block_given?; end
  def bar; p :nok if block_given?; end
  foo(bar) {}
}

assert ':ok', %{
  def foo(x=bar); p :ok if block_given?; end
  def bar; p :nok if block_given?; end
  foo {}
}

assert ':ok', %{
  class X
    def foo; p :nok if block_given?; self; end
    def bar; p :ok if block_given?; self; end
  end
  X.new.foo.bar {}
}

assert ':ok', %{
  def foo; Proc.new; end; foo { p :ok }.call
}

assert ':ok', %{
  def foo(x=Proc.new); x.call; end; foo { p :ok }
}

assert ':ok', %{
  class X
    def initialize(x=Proc.new); x.call; end
  end
  X.new { p :ok }
}

assert '3', %{
  def foo(a=Proc.new, b=Proc.new, c=Proc.new)
    a.call; b.call; c.call
  end
  i = 0
  foo { i+=1 }
  p i
}

assert ':ok', %{
  def foo(x=Proc.new); x.call; end
  def bar; foo; end
  begin
    bar { p :nok }
  rescue ArgumentError
    p :ok
  end
}

assert ':ok', %{
  def a
    yield
  end
  def b(&block)
    a(&block)
  end
  def c
    b do
      yield
    end
  end
  c { p :ok }
}

assert ':ok', %{
  def a
    1.times do yield end
  end
  def b(&block)
    a(&block)
  end
  def c
    b do
      yield
    end
  end
  c { p :ok }
}

assert '42', %{
  def foo; 1.times { return 42 }; p :nok; end
  p foo
}

assert '42', %{
  def foo; 1.times { 1.times { 1.times { return 42 } } }; p :nok; end
  p foo
}

assert '42', %{
  def foo; 1.times { return yield }; end
  def bar; foo { return 42 }; p :nok; end
  p bar
}

assert ':ok', %{
  def foo
    begin
      yield
    ensure
      p :ok
    end
  end
  def bar
   foo { return }
  end 
  bar
}

assert 'false', %{
  def foo(m); m.synchronize { return 42 }; end
  m = Mutex.new
  foo(m)
  p m.locked?
}

assert ':ok', %{
  def foo(v)
    1.times do
      return true if v
      return false
      p :nok1
    end
    p :nok2
  end
  p :ok if !foo(false) and foo(true)
}

assert ":ok\n:ok", %{
  def foo
    raise
  rescue
    return 42
    omgwtf
  end
  p :ok if foo == 42
  p :ok if $!.nil?
}

assert ':ok', %{
  b = :foo.to_proc
  begin
    b.call
  rescue ArgumentError
    p :ok
  end
}

assert '[2, 3, 4]', "p [1, 2, 3].map(&:succ)"

assert '42', %{
  b = proc { |x| if x then p x else b.call(42) end }
  b.call(nil)
}

assert '42', %{
  b = nil
  1.times {
    x = 42
    b = proc { p x }
  }
  x = 1
  b.call
}

assert "42\n42", %{
  a = b = c = nil
  1.times {
    1.times {
      x = 42
      1.times {
        1.times {
          c = proc {
            a = proc { p x }
            b = proc { p x }
          }
        }
        c.call
      }
    }
  }
  b.call
  a.call
}

assert '42', %{
  END { p 42 }
}

assert '42', %{
  $x = 42
  END { $x += 1 }
  END { p $x }
}
