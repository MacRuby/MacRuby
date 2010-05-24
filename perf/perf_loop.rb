perf_test('while') do
  i = 0
  while i < 10000
    j = 0
    while j < 2000
      j += 1
    end
    i += 1
  end
end

perf_test('times') do
  10000.times do
    2000.times do
    end
  end
end

perf_test('for') do
  for i in 0..10000
    for j in 0..2000
    end
  end
end

perf_test('upto') do
  0.upto(10000) do
    0.upto(2000) do
    end
  end
end
