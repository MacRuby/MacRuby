perf_test('new') do
  i = 0
  while i < 1000000
    b = {}
    c = {1 => i}
    d = {1 => i, 2 => i}
    i += 1
  end
end

perf_test('[]') do
  i = 0
  a = {0=>:zero, 1=>:un, 2=>:deux, 3=>:trois, 4=>:quatre}
  while i < 1000000
    a[0]; a[1]; a[2]; a[3]; a[4]
    a[0]; a[1]; a[2]; a[3]; a[4]
    a[0]; a[1]; a[2]; a[3]; a[4]
    a[0]; a[1]; a[2]; a[3]; a[4]
    i += 1
  end
end

perf_test('[]=') do
  i = 0
  a = {}
  while i < 1000000
    a[0] = a[1] = a[2] = a[3] = a[4] = i + 1
    a[0] = a[1] = a[2] = a[3] = a[4] = i - 1
    a[0] = a[1] = a[2] = a[3] = a[4] = i + 1
    a[0] = a[1] = a[2] = a[3] = a[4] = i - 1
    i += 1
  end
end
