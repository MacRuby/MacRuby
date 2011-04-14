n = (0..9999).to_a.inject(0) { |b, i| b + i }.to_s

# sequential + synchronous
assert n, %{
  @n = 0
  q = Dispatch::Queue.new('foo')
  10000.times do |i|
    q.sync { @n += i }
  end
  p @n
}

# sequential + asynchronous
assert n, %{
  @n = 0
  q = Dispatch::Queue.new('foo')
  10000.times do |i|
    q.async { @n += i }
  end
  q.sync {}
  p @n
}

# group + sequential
assert n, %{
  @n = 0
  q = Dispatch::Queue.new('foo')
  g = Dispatch::Group.new
  10000.times do |i|
    q.async(g) { @n += i }
  end
  g.wait
  p @n
}

# group + concurrent
assert n, %{
  @n = 0
  q = Dispatch::Queue.new('foo')
  g = Dispatch::Group.new
  10000.times do |i|
    Dispatch::Queue.concurrent.async(g) do
      q.sync { @n += i }
    end
  end
  g.wait(10)
  p @n
}, :known_bug => true

# apply + sequential
assert n, %{
  @n = 0
  q = Dispatch::Queue.new('foo')
  q.apply(10000) do |i|
    @n += i
  end
  p @n
}

# apply + concurrent
assert n, %{
  @n = 0
  q = Dispatch::Queue.new('foo')
  Dispatch::Queue.concurrent.apply(10000) do |i|
    q.async { @n += i }
  end
  q.sync {}
  p @n
}

# exceptions should be ignored
assert ':ok', %{
  g = Dispatch::Group.new
  Dispatch::Queue.concurrent.async(g) { raise('hey') }
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
    Dispatch::Queue.concurrent.async(g) { i+2*3/4-5 }
    i += 1
  end
  g.wait
  p :ok
}
