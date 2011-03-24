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

perf_test('msplat') do
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

perf_test('dsplat') do
  o = TestMethod.new
  i = 0
  a = [1,2,3,4,5,6]
  while i < 1000000
    o.args_method(*a); o.args_method(*a); o.args_method(*a)
    o.args_method(*a); o.args_method(*a); o.args_method(*a)
    o.args_method(*a); o.args_method(*a); o.args_method(*a)
    o.args_method(*a); o.args_method(*a); o.args_method(*a)
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

class TestPolyMethod1
  def foo; 1; end
end

class TestPolyMethod2
  def foo; 2; end
end

class TestPolyMethod3 < TestPolyMethod1
  def foo; 3; end
end

class TestPolyMethod4 < TestPolyMethod2
  def foo; 4; end
end

perf_test('poly') do
  o1 = TestPolyMethod1.new
  o2 = TestPolyMethod2.new
  o3 = TestPolyMethod3.new
  o4 = TestPolyMethod4.new
  i = 0
  while i < 1000000
    o1.foo; o2.foo; o3.foo; o4.foo
    o1.foo; o2.foo; o3.foo; o4.foo
    o1.foo; o2.foo; o3.foo; o4.foo
    o1.foo; o2.foo; o3.foo; o4.foo
    i += 1
  end
end

class TestSuper1
  def foo; 42; end
end

class TestSuper2 < TestSuper1
  def foo; super; end
end

class TestSuper3 < TestSuper2
  def foo; super; end
end

class TestSuper4 < TestSuper3
  def foo; super; end
end

perf_test('super') do
  o4 = TestSuper4.new
  o3 = TestSuper3.new
  i = 0
  while i < 10000
    o4.foo; o3.foo; o4.foo; o3.foo
    o4.foo; o3.foo; o4.foo; o3.foo
    o4.foo; o3.foo; o4.foo; o3.foo
    o4.foo; o3.foo; o4.foo; o3.foo
    i += 1
  end
end
