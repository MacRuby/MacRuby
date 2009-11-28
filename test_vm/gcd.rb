n = (0..9999).to_a.inject(0) { |b, i| b + i }.to_s

# sequential + synchronous
assert n, %{
  n = 0
  q = Dispatch::Queue.new('foo')
  10000.times do |i|
    q.dispatch(true) { n += i }
  end
  p n
}

# sequential + asynchronous
assert n, %{
  n = 0
  q = Dispatch::Queue.new('foo')
  10000.times do |i|
    q.dispatch { n += i }
  end
  q.dispatch(true) {}
  p n
}

# group + sequential
assert n, %{
  n = 0
  q = Dispatch::Queue.new('foo')
  g = Dispatch::Group.new
  10000.times do |i|
    g.dispatch(q) { n += i }
  end
  g.wait
  p n
}

# group + concurrent
assert n, %{
  n = 0
  q = Dispatch::Queue.new('foo')
  g = Dispatch::Group.new
  10000.times do |i|
    g.dispatch(Dispatch::Queue.concurrent) do
      q.dispatch(true) { n += i }
    end
  end
  g.wait
  p n
}

# apply + sequential
assert n, %{
  n = 0
  q = Dispatch::Queue.new('foo')
  q.apply(10000) do |i|
    n += i
  end
  p n
}

# apply + concurrent
assert n, %{
  n = 0
  q = Dispatch::Queue.new('foo')
  Dispatch::Queue.concurrent.apply(10000) do |i|
    q.dispatch { n += i }
  end
  q.dispatch(true) {}
  p n
}

# exceptions should be ignored
assert ':ok', %{
  g = Dispatch::Group.new
  g.dispatch(Dispatch::Queue.concurrent) { raise('hey') }
  g.wait
  p :ok
}

# Stress tests

assert ':ok', %{
  Dispatch::Queue.concurrent.apply(10000000) { |i| i+2*3/4-5}
  p :ok
}

assert ':ok', %{
  i = 0
  g = Dispatch::Group.new
  while i < 100000
    g.dispatch(Dispatch::Queue.concurrent) { i+2*3/4-5 }
    i += 1
  end
  g.wait
  p :ok
}
