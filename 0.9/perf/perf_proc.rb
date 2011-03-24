perf_test('call+noarg') do
  p = Proc.new { 42 }
  i = 0
  while i < 1000000
    p.call; p.call; p.call; p.call; p.call
    p.call; p.call; p.call; p.call; p.call
    p.call; p.call; p.call; p.call; p.call
    p.call; p.call; p.call; p.call; p.call
    i += 1
  end
end

perf_test('call+args') do
  p = Proc.new { |a, b, c| 42 }
  i = 0
  while i < 1000000
    p.call(i, 2, 3); p.call(1, i, 3); p.call(1, 2, i); p.call(i, i, i)
    p.call(i, 2, 3); p.call(1, i, 3); p.call(1, 2, i); p.call(i, i, i)
    p.call(i, 2, 3); p.call(1, i, 3); p.call(1, 2, i); p.call(i, i, i)
    p.call(i, 2, 3); p.call(1, i, 3); p.call(1, 2, i); p.call(i, i, i)
    i += 1
  end
end

perf_test('call+splat') do
  p = Proc.new { |*a| 42 }
  i = 0
  while i < 100000
    p.call(i)
    p.call(i, i)
    p.call(i, i, i)
    p.call(i, i, i, i)
    p.call(i, i, i, i, i)
    p.call(i, i, i, i)
    p.call(i, i, i)
    p.call(i, i)
    p.call(i)
    i += 1
  end
end
