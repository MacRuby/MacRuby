assert ":ok", "begin; p :ok; rescue; end"
assert ":ok", "begin; raise; p :nok; rescue; p :ok; end"
assert ":ok", %q{
  def m; begin; raise; ensure; p :ok; end; end
  begin; m; rescue; end
}

assert "42", "x = 40; begin; x += 1; rescue; ensure; x += 1; end; p x"
assert "42", "x = 40; begin; raise; x = nil; rescue; x += 1; ensure; x += 1; end; p x"

assert "42", "x = 40; begin; x; rescue => e; else; x = 42 ; end; p x"
assert ":ok", "p begin; :ko; rescue => e; :ko; else; :ok; end"

assert "42", "x = begin; 42; rescue; nil; end; p x"
assert "42", "x = begin; raise; nil; rescue; 42; end; p x"
assert "42", "x = begin; 42; rescue; nil; ensure; nil; end; p x"
assert "42", "x = begin; raise; nil; rescue; 42; ensure; nil; end; p x"

assert "42", "x = 40; begin; x += 1; raise; rescue; retry if x < 42; end; p x"

assert ":ok", %q{
  begin
    raise
  rescue => e
    p :ok if e.is_a?(RuntimeError)
  end
}

assert ":ok", %q{
  begin
    raise
  rescue => e
  end
  p :ok if e.is_a?(RuntimeError)
}

assert ":ok", %q{
  begin
    raise 'foo'
  rescue => e
    p :ok if e.is_a?(RuntimeError) and e.message == 'foo'
  end
}

assert ":ok", %q{
  class X < StandardError; end
  exc = X.new
  begin
    raise exc
  rescue => e
    p :ok if e == exc
  end
}

assert ":ok", %q{
  class X < StandardError; end
  class Y < X; end
  class Z < Y; end
  begin
    raise Y
  rescue Z
    p :nok
  rescue X
    p :ok
  end
}

assert ":ok", %q{
  begin
    begin
      raise LoadError
    rescue
      p :nok
    end
  rescue LoadError => e
    p :ok if e.is_a?(LoadError)
  end
}

assert ":ok", %q{
  begin
    self.foo
  rescue => e
    p :ok if e.is_a?(NoMethodError)
  end
}

assert ":ok", %q{
  begin
    foo
  rescue => e
    p :ok if e.is_a?(NameError)
  end
}

assert ":ok", %q{
  begin
    1.times { raise }
  rescue
    p :ok
  end
}

assert ":ok", %q{
  begin
    def foo; raise; end
    foo
  rescue
    p :ok
  end
}

assert ":ok", "1.times { x = foo rescue nil; }; p :ok"

# the code inside an ensure is even executed
# if we leave the ensure block with a throw
assert ':ok', %{
  catch(:a) do
    begin
      throw :a
    ensure
      p :ok
    end
  end  
}

# the code inside an ensure is even executed
# if we leave the ensure block with a return
assert ':ok', %{
  def foo
    begin
      return
    ensure
      p :ok
    end
  end
  foo
}

assert ':ok', %{
  p :ok if $!.nil?
}

assert ':ok', %{
  begin
    p :ok if $!.nil?
    raise
  rescue
  end
}

assert ':ok', %{
  begin
    raise 'foo'
  rescue
  end
  p :ok if $!.nil?
}

assert ':ok', %{
  begin
    raise 'foo'
  rescue => e
  end
  p :ok if $!.nil?
}

assert ':ok', %{
  e = RuntimeError.new('foo')
  begin
    raise e
  rescue
    p :ok if $!.eql?(e)
  end
}

assert ':ok', %{
  begin
    raise 'foo'
  rescue => e
    p :ok if $!.eql?(e)
  end
}

assert ":ok\n:ok\n:ok", %{
  e1 = RuntimeError.new('e1')
  e2 = RuntimeError.new('e2')
  e3 = RuntimeError.new('e3')
  begin
    raise e1
  rescue
    begin
      raise e2
    rescue
      begin
        raise e3
      rescue
        p :ok if $!.eql?(e3)
      end
      p :ok if $!.eql?(e2)
    end
    p :ok if $!.eql?(e1)
  end
}

assert ":ok\n:ok\n:ok\n:ok", %{
  x = 0
  e = RuntimeError.new('foo')
  begin
    p :ok if $!.nil?
    x += 1
    raise e
  rescue
    p :ok if $!.eql?(e)
    retry if x < 2
  end
}

assert ":ok", %{
  begin
    eval("1==2==3")
  rescue Exception
  end
  p :ok if $!.nil?
}

assert ":ok", %{
  e = RuntimeError.new('foo')
  begin
    raise e
  rescue
    begin
      raise
    rescue => e2
      p :ok if e.eql?(e2)
    end
  end
}

assert ":ok", %{
  module M
    class LoadError < ::LoadError
    end
  end
  class M::Foo
    begin
      require 'hey'
    rescue LoadError
      p :ok
    end
  end
}

assert '42', %{
  def bar(arg)
    return 42 if arg == 1
    return a
  ensure
    nil
  end
  p bar(1)
}

assert '42', %{
  def foo
    begin
      return 42
    ensure
      nil
    end
    23
  end
  p foo
}

assert "1\n2\n42", %{
  def foo
    begin
      return 42
    ensure
      p 1
    end
    p 3
  ensure
    p 2
  end
  p foo
}

assert ':ok', %{
  class A < Exception; end
  def foo
    begin
      raise 'foo'
    rescue A
    ensure
      p :ok
    end
    p :ko
  end
  
  begin
    foo
  rescue
  end  
}

assert ':ok', %{
  class A < Exception; end
  def foo
    begin
      begin
        raise 'foo'
      rescue A
      end
      p :ko
    ensure
      p :ok
    end
    p :ko
  end
  
  begin
    foo
  rescue
  end
}

assert "1\n2", %{
  class A < Exception; end
  class B < Exception; end
  begin
    begin
      begin
        raise A.new
      rescue B
        p :ko1
      end
      p :ko2
    rescue B
      p :ko3
    end
    p :ko4
  rescue A
    p 1
  ensure
    p 2
  end
}

assert ":ok", %{
  class A < Exception; end
  class B < Exception; end
  
  begin
    begin
      raise B.new
    rescue A
      p :ko
    end
    p :ko
  rescue B
    p :ok
  end
}

assert '42', %{
  class A < Exception; end
  class B < Exception; end
  
  def foo
    a = 42
    $p = proc { p a }
    raise A.new
  rescue B
  end
  
  begin
    foo
  rescue A
  end
  $p.call  
}

assert "b0\na1", %{
  def foo
    2.times do |i|
      begin
        raise StandardError if i == 0
        puts "a\#{i}"
        return
      rescue StandardError
        puts "b\#{i}"
        next
      end
    end
  end
  foo
}

# Ensure should be called only once
assert ":ok", %{
  def foo
    raise
  ensure
    p :ok
    return true
  end
  foo
}, :known_bug => true

assert ":ok", %{
  def foo
    begin
      raise "bad"
    rescue
      raise "ok"
    end
  end
  begin
    foo
  rescue => e
    p e.message.intern
  end
}
