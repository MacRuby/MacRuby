assert ":ok", "begin; p @@foo; rescue NameError; p :ok; end"
assert "42",  "@@foo = 42; p @@foo"

assert "42", %{
  class X
    @@foo = 41
    def foo; @@foo; end
  end
  class Y < X
    @@foo += 1
  end
  p X.new.foo
}
