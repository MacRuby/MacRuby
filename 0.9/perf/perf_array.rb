perf_test('new') do
  i = 0
  while i < 1000000
    b = [i]
    c = [i, i]
    d = [i, i, i]
    i += 1
  end
end

perf_test('<<') do
  i = 0
  a = []
  while i < 1000000
    a << 1; a << 2; a << 3; a << 4; a << 5
    a << 5; a << 4; a << 3; a << 2; a << 1
    a << 1; a << 2; a << 3; a << 4; a << 5
    a << 5; a << 4; a << 3; a << 2; a << 1
    i += 1
  end
end

perf_test('[]') do
  i = 0
  a = [1, 2, 3, 4, 5]
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
  a = [1, 2, 3, 4, 5]
  while i < 1000000
    a[0] = a[1] = a[2] = a[3] = a[4] = i + 1
    a[0] = a[1] = a[2] = a[3] = a[4] = i - 1
    a[0] = a[1] = a[2] = a[3] = a[4] = i + 1
    a[0] = a[1] = a[2] = a[3] = a[4] = i - 1
    i += 1
  end
end
