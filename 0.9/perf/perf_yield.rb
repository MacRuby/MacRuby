class TestBlock
  def yield_noarg
    i = 0
    while i < 1000000
      yield; yield; yield; yield; yield
      yield; yield; yield; yield; yield
      yield; yield; yield; yield; yield
      yield; yield; yield; yield; yield
      i += 1
    end
  end

  def yield_args(n=1000000)
    i = 0
    while i < n
      yield(i, 2, 3); yield(1, i, 3); yield(1, 2, i); yield(i, i, i)
      yield(i, 2, 3); yield(1, i, 3); yield(1, 2, i); yield(i, i, i)
      yield(i, 2, 3); yield(1, i, 3); yield(1, 2, i); yield(i, i, i)
      yield(i, 2, 3); yield(1, i, 3); yield(1, 2, i); yield(i, i, i)
      i += 1
    end
  end
end

perf_test('noarg') do
  o = TestBlock.new
  o.yield_noarg { 42 }
end

perf_test('same_arity') do
  o = TestBlock.new
  o.yield_args { |a, b, c| 42 }
end

perf_test('less_arity') do
  o = TestBlock.new
  o.yield_args(200000) { |a, b| 42 }
end

perf_test('more_arity') do
  o = TestBlock.new
  o.yield_args(200000) { |a, b, c, e| 42 }
end

perf_test('splat') do
  o = TestBlock.new
  o.yield_args(50000) { |*a| 42 }
end
