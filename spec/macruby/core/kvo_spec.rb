require File.dirname(__FILE__) + "/../spec_helper"

class Wrapper
  attr_accessor :whatever

  def initialize(value)
    super()
    @wrapped = value
    @whatever= 'like, whatever'
  end

  def wrappedValue
    @wrapped
  end
end

class FancyWrapper < NSValue
  attr_accessor :whatever

  def initialize(value)
    super()
    @wrapped = value
    @whatever= 'like, whatever'
  end

  def wrappedValue
    @wrapped
  end
end

describe "An Object being observed through NSKeyValueObservation" do
  it "retains the values for its instance variables" do
    #
    # Was <rdar://problem/7210942> 
    # 
    w = Wrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    w.wrappedValue.should == 42
  end

  it "keeps reporting its instance variables through instance_variables" do
    #
    # <rdar://problem/7210942> 
    # 
    w = Wrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    w.instance_variables.should include(:@wrapped)
  end

  it "can be inspected" do
    #
    # <rdar://problem/7210942> 
    # 
    w = Wrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    lambda { w.inspect }.should_not raise_error
  end
end

describe "A nontrivially derived Object" do
  it "retains the values for its instance variables" do
    w = FancyWrapper.new(42)
    w.wrappedValue.should == 42
  end
end

describe "A nontrivially derived Object being observed through NSKeyValueObservation" do
  it "retains the values for its instance variables" do
    #
    # <rdar://problem/7260995>
    # 
    w = FancyWrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    w.wrappedValue.should == 42
  end

  it "keeps reporting its instance variables through instance_variables" do
    w = FancyWrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    w.instance_variables.should include(:@wrapped)
  end

  it "can be inspected" do
    w = FancyWrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    lambda { w.inspect }.should_not raise_error
  end
end
