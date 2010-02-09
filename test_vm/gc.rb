# Testing the TLC.

assert 'true', %{
  s = nil
  Thread.new {
    s = [1,2,3,4,5]
    GC.start
  }.join
  p s == [1,2,3,4,5]
}

assert 'true', %{
  s = nil
  Thread.new {
    1.times { s = [1,2,3,4,5]; GC.start }
  }.join
  p s == [1,2,3,4,5]
}

assert 'true', %{
  def foo
    Thread.new { yield }.join
  end
  s = nil
  foo { s = [1,2,3,4,5]; GC.start }
  p s == [1,2,3,4,5]
}

assert 'true', %{
  def bar
    Thread.new { yield }.join
  end
  def foo
    bar { yield }
  end
  s = nil
  foo { s = [1,2,3,4,5]; GC.start }
  p s == [1,2,3,4,5]
}
