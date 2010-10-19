assert "6", %{
def f
  self.inspect
  puts caller.join('').scan(/(\\d+):/)
end
f
}

assert "7\n12", %{
class A
  def f
    puts caller.join('').scan(/(\\d+):/)
  end
  def g
    f
    self.inspect
  end
end
a = A.new
a.g
}

assert "10\n14", %{
class A            # line 1
  def f
    caller.join('').scan(/(\\d+):/)
  end
  def g
    1
    self.inspect
    2
    f
  end
end
a = A.new
puts a.g
}

assert "9\n6\n8", %{
  def foo
    puts caller.join('').scan(/(\\d+):/)
  end
  def bar
    yield
  end
  bar do
    foo
  end
}
