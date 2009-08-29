require File.dirname(__FILE__) + "/../spec_helper"

describe "Dispatch::Queue.concurrent" do
  it "returns an instance of Queue" do
    o = Dispatch::Queue.concurrent
    o.should be_kind_of(Dispatch::Queue)
  end

  it "can accept a symbol argument which represents the priority" do
    o = Dispatch::Queue.concurrent(:low)
    o.should be_kind_of(Dispatch::Queue)
 
    o = Dispatch::Queue.concurrent(:default)
    o.should be_kind_of(Dispatch::Queue)

    o = Dispatch::Queue.concurrent(:high)
    o.should be_kind_of(Dispatch::Queue)

    lambda { Dispatch::Queue.concurrent(42) }.should raise_error(TypeError)
  end

  it "raises an ArgumentError if the given argument is not a valid priority symvol" do
    lambda { Dispatch::Queue.concurrent(:foo) }.should raise_error(ArgumentError)
  end
end

describe "Dispatch::Queue.current" do
  it "returns an instance of Queue" do
    o = Dispatch::Queue.current
    o.should be_kind_of(Dispatch::Queue)
  end
end

describe "Dispatch::Queue.main" do
  it "returns an instance of Queue" do
    o = Dispatch::Queue.main
    o.should be_kind_of(Dispatch::Queue)
  end
end

describe "Dispatch::Queue.new" do
  it "accepts a name and returns an instance of Queue" do
    o = Dispatch::Queue.new('foo')
    o.should be_kind_of(Dispatch::Queue)

    lambda { Dispatch::Queue.new('foo', 42) }.should raise_error(ArgumentError)
    lambda { Dispatch::Queue.new(42) }.should raise_error(TypeError)
  end
end

describe "Dispatch::Queue#dispatch" do
  it "accepts a block and yields it asynchronously" do
    o = Dispatch::Queue.new('foo')
    i = 0
    o.dispatch { i = 42 }
    while i == 0 do; end
    i.should == 42
  end

  it "accepts a block and yields it synchronously if the given argument is true" do
    o = Dispatch::Queue.new('foo')
    i = 0
    o.dispatch(true) { i = 42 }
    i.should == 42
  end

  it "raises an ArgumentError if no block is given" do
    o = Dispatch::Queue.new('foo')
    lambda { o.dispatch }.should raise_error(ArgumentError) 
    lambda { o.dispatch(true) }.should raise_error(ArgumentError) 
  end
end

describe "Dispatch::Queue#apply" do
  it "accepts an input size and a block and yields it as many times" do
    o = Dispatch::Queue.new('foo')
    i = 0
    o.apply(10) { i += 1 }
    i.should == 10
    i = 42
    o.apply(0) { i += 1 }
    i.should == 42

    lambda { o.apply(nil) {} }.should raise_error(TypeError) 
  end

  it "raises an ArgumentError if no block is given" do
    o = Dispatch::Queue.new('foo')
    lambda { o.apply(42) }.should raise_error(ArgumentError) 
  end
end

describe "Dispatch::Queue#after" do
  it "accepts a given time (in seconds) and a block and yields it after" do
    o = Dispatch::Queue.new('foo')
    i = 0
    t = Time.now
    o.after(0.2) { i = 42 }
    while i == 0 do; end
    t2 = Time.now - t
    t2.should >= 0.2
    t2.should < 0.5
    i.should == 42

    lambda { o.after(nil) {} }.should raise_error(TypeError) 
  end

  it "raises an ArgumentError if no block is given" do
    o = Dispatch::Queue.new('foo')
    lambda { o.after(42) }.should raise_error(ArgumentError) 
  end
end

describe "Dispatch::Queue#label" do
  it "returns the name of the queue" do
    o = Dispatch::Queue.new('foo')
    o.label.should == 'foo'

    o = Dispatch::Queue.main
    o.label.should == 'com.apple.main-thread'
  end
end

describe "Dispatch::Queue#suspend!" do
  it "suspends the queue which can be resumed by calling #resume!" do
    o = Dispatch::Queue.new('foo')
    o.dispatch { sleep 1 }
    o.suspended?.should == false
    o.suspend! 
    o.suspended?.should == true
    o.resume!
    o.suspended?.should == false
  end
end

# TODO:
#  Dispatch::Queue.main.run
#  Dispatch::Group
#  Dispatch::Source
