class TestMethod
  def empty_method
  end

  def noarg_method
    42
  end

  def args_method(a, b, c, d, e, f)
    42
  end

  def splat_method(*a)
    42
  end

  def opt_method(a, b=1, c=2, d=3, e=4)
    42
  end
end

perf_test('empty') do
  o = TestMethod.new
  i = 0
  while i < 1000000
    o.empty_method; o.empty_method; o.empty_method; o.empty_method;
    o.empty_method; o.empty_method; o.empty_method; o.empty_method;
    o.empty_method; o.empty_method; o.empty_method; o.empty_method;
    o.empty_method; o.empty_method; o.empty_method; o.empty_method;
    i += 1
  end
end

perf_test('noarg') do
  o = TestMethod.new
  i = 0
  while i < 1000000
    o.noarg_method; o.noarg_method; o.noarg_method; o.noarg_method;
    o.noarg_method; o.noarg_method; o.noarg_method; o.noarg_method;
    o.noarg_method; o.noarg_method; o.noarg_method; o.noarg_method;
    o.noarg_method; o.noarg_method; o.noarg_method; o.noarg_method;
    i += 1
  end
end

perf_test('args') do
  o = TestMethod.new
  i = 0
  while i < 1000000
    o.args_method(i, 1, i, 2, i, 3); o.args_method(i, 1, i, 2, i, 3)
    o.args_method(i, 3, i, 2, i, 1); o.args_method(i, 3, i, 2, i, 1)
    o.args_method(i, 1, i, 2, i, 3); o.args_method(i, 1, i, 2, i, 3)
    o.args_method(i, 3, i, 2, i, 1); o.args_method(i, 3, i, 2, i, 1) 
    o.args_method(i, 1, i, 2, i, 3); o.args_method(i, 1, i, 2, i, 3)
    i += 1
  end
end

perf_test('splat') do
  o = TestMethod.new
  i = 0
  while i < 200000
    o.splat_method(i)
    o.splat_method(i, i)
    o.splat_method(i, i, i)
    o.splat_method(i, i, i, i)
    o.splat_method(i, i, i, i, i)
    o.splat_method(i, i, i, i)
    o.splat_method(i, i, i)
    o.splat_method(i, i)
    o.splat_method(i)
    i += 1
  end
end

perf_test('opt') do
  o = TestMethod.new
  i = 0
  while i < 1000000
    o.opt_method(i)
    o.opt_method(i, i)
    o.opt_method(i, i, i)
    o.opt_method(i, i, i, i)
    o.opt_method(i, i, i, i, i)
    o.opt_method(i, i, i, i)
    o.opt_method(i, i, i)
    o.opt_method(i, i)
    o.opt_method(i)
    i += 1
  end
end
