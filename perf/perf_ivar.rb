class TestIvars
  def initialize
    @foo1 = 1; @foo2 = 2; @foo3 = 3; @foo4 = 4; @foo5 = 5
  end

  def test_get
    i = 0
    while i < 2000000
      @foo1; @foo2; @foo3; @foo4; @foo5; @foo5; @foo4; @foo3; @foo2; @foo1
      @foo1; @foo2; @foo3; @foo4; @foo5; @foo5; @foo4; @foo3; @foo2; @foo1
      @foo1; @foo2; @foo3; @foo4; @foo5; @foo5; @foo4; @foo3; @foo2; @foo1
      @foo1; @foo2; @foo3; @foo4; @foo5; @foo5; @foo4; @foo3; @foo2; @foo1
      @foo1; @foo2; @foo3; @foo4; @foo5; @foo5; @foo4; @foo3; @foo2; @foo1
      i += 1
    end
  end

  def test_set
    i = 0
    while i < 2000000
      @foo1 = @foo2 = @foo3 = @foo4 = @foo5 = i + 1;
      @foo1 = @foo2 = @foo3 = @foo4 = @foo5 = i - 1;
      @foo1 = @foo2 = @foo3 = @foo4 = @foo5 = i + 1;
      @foo1 = @foo2 = @foo3 = @foo4 = @foo5 = i - 1;
      i += 1
    end
  end
end

perf_test('get') { TestIvars.new.test_get }
perf_test('set') { TestIvars.new.test_set }

class TestAttrs
  attr_accessor :foo1, :foo2, :foo3, :foo4, :foo5
  def initialize
    @foo1 = 1; @foo2 = 2; @foo3 = 3; @foo4 = 4; @foo5 = 5
  end

  def test_reader
    i = 0
    while i < 500000
      foo1; foo2; foo3; foo4; foo5; foo5; foo4; foo3; foo2; foo1
      foo1; foo2; foo3; foo4; foo5; foo5; foo4; foo3; foo2; foo1
      foo1; foo2; foo3; foo4; foo5; foo5; foo4; foo3; foo2; foo1
      foo1; foo2; foo3; foo4; foo5; foo5; foo4; foo3; foo2; foo1
      foo1; foo2; foo3; foo4; foo5; foo5; foo4; foo3; foo2; foo1
      i += 1
    end 
  end

  def test_writer
    i = 0
    while i < 1000000
      self.foo1 = self.foo2 = self.foo3 = self.foo4 = self.foo5 = i + 1;
      self.foo1 = self.foo2 = self.foo3 = self.foo4 = self.foo5 = i - 1;
      self.foo1 = self.foo2 = self.foo3 = self.foo4 = self.foo5 = i + 1;
      self.foo1 = self.foo2 = self.foo3 = self.foo4 = self.foo5 = i - 1;
      i += 1
    end
  end
end

perf_test('attr_reader') { TestAttrs.new.test_reader }
perf_test('attr_writer') { TestAttrs.new.test_writer }
