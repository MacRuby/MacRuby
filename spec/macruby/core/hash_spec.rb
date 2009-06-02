require File.dirname(__FILE__) + "/../spec_helper"

describe "The Hash class" do
  it "is an alias to NSMutableDictionary" do
    Hash.should == NSMutableDictionary
  end

  it "can be subclassed and later instantiated" do
    k = Class.new(Hash)
    a = k.new
    a.class.should == k
    a[42] = 123
    a[42].should == 123
  end
end

describe "The NSDictionary class" do
  it "can be subclassed and later instantiated" do
    k = Class.new(NSDictionary)
    a = k.new
    a.class.should == k
    a.size.should == 0
    lambda { a[42] = 123 }.should raise_error(RuntimeError)
  end
end

describe "An Hash object" do
  it "is an instance of the Hash/NSMutableDictionary class" do
    {}.class.should == Hash
    {}.kind_of?(Hash).should == true
    {}.instance_of?(Hash).should == true
  end

  it "is mutable" do
    a = {}
    a[42] = 123
    a[42].should == 123
  end

  it "can have a singleton class" do
    a = {}
    def a.foo; 42; end
    a.foo.should == 42
    a[42] = 123
    a[42].should == 123
  end
end

describe "An NSDictionary object" do
  it "is an instance of the NSDictionary class" do
    a = NSDictionary.dictionary
    a.class.should == NSDictionary
    a = NSDictionary.dictionaryWithObject(42, forKey:42)
    a.class.should == NSDictionary
  end

  it "is immutable" do
    a = NSDictionary.dictionary
    a.size.should == 0
    lambda { a[42] = 123 }.should raise_error(RuntimeError)
  end

  it "can have a singleton class" do
    a = NSDictionary.array
    def a.foo; 42; end
    a.foo.should == 42
    lambda { a[42] = 123 }.should raise_error(RuntimeError)
  end
end
