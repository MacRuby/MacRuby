assert ":ok", "begin; p :ok; rescue; end"
assert ":ok", "begin; raise; p :nok; rescue; p :ok; end"
assert ":ok", %q{
  def m; begin; raise; ensure; p :ok; end; end
  begin; m; rescue; end
}

assert "42", "x = 40; begin; x += 1; rescue; ensure; x += 1; end; p x"
assert "42", "x = 40; begin; raise; x = nil; rescue; x += 1; ensure; x += 1; end; p x"

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
}, :known_bug => true

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
