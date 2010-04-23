require File.dirname(__FILE__) + "/../spec_helper"

class TestThreadTarget
  attr_reader :value

  def start1(mutex)
    mutex.lock
    @value = 42
  end

  def start2(n)
    (n * 10000).times { foo }
  end

  def foo; 42; end
end

describe "NSThreads" do
  it "allows to perform a selector on a new background thread" do
    target = TestThreadTarget.new
    mutex = Mutex.new
    mutex.lock
    t = NSThread.alloc.initWithTarget(target, selector: :"start1:", object: mutex)
    t.executing?.should == false
    target.value.should == nil
    t.start
    t.executing?.should == true
    mutex.unlock
    while t.executing?; end
    t.finished?.should == true
    target.value.should == 42
  end

  it "can call into MacRuby methods in a reentrant way" do
    target = TestThreadTarget.new
    thrs = []
    n = 9
    while n >= 0
      t = NSThread.alloc.initWithTarget(target, selector: :"start2:", object: n)
      t.start
      n -= 1
      thrs << t
    end

    thrs.each do |t|
      while t.executing?; end
      t.finished?.should == true
    end
  end
end

class TestOperation < NSOperation
  attr_accessor :value

  def main
    (@value * 1000).times { foo }
  end

  def foo; 42; end
end

# NOTE: we aren't really testing the whole NSOperation API here, just making
# sure the basic stuff is working. NSOperation on recent Mac OS X versions is
# anyway supposed to use GCD which is well covered already.
describe "NSOperations" do
  it "allows to share a group of threads for concurrent logic" do
    queue = NSOperationQueue.new
    queue.maxConcurrentOperationCount = 10

    ops = []
    100.times do |i|
      op = TestOperation.new
      op.value = i
      op.executing?.should == false
      queue.addOperation(op)
      ops << op
    end

    queue.waitUntilAllOperationsAreFinished
    queue.operations.size.should == 0
    ops.each { |op| op.finished?.should == true }
  end
end
