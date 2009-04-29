assert ":ok", "1.times { p :ok }"
assert "42",  "p 42.times {}"
assert "42",  "i = 0; 42.times { i += 1 }; p i"
assert "42",  "x = nil; 43.times { |i| x = i }; p x"
assert "",    "0.times { p :nok }"

assert ":ok", "def foo; yield; end; foo { p :ok }"
assert "42",  "def foo; yield 42; end; foo { |x| p x }"
assert "",    "def foo; end; foo { p :nok }" 
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
assert "42", "begin p proc { break 24 }.call rescue LocalJumpError; p 42 end"

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

# Enumerator 
assert "[\"f\", \"o\", \"o\"]", "p 'foo'.chars.to_a"
