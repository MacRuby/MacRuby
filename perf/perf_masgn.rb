perf_test('symetric') do
  i = 0
  x = 1
  y = 2
  while i < 200000
    x, y = y, x
    y, x = x, y
    x, y = y, x
    y, x = x, y
    x, y = y, x
    y, x = x, y
    x, y = y, x
    y, x = x, y
    i += 1
  end
end
