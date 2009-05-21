require File.dirname(__FILE__) + "/../spec_helper"

describe "The String class" do
  it "is an alias to NSMutableString" do
    String.should == NSMutableString
  end

  it "can be subclassed and later instantiated" do
    k = Class.new(String)
    a = k.new
    a.class.should == k
    a << 'foo'
    a.should == 'foo'
  end
end

describe "The NSString class" do
  it "can be subclassed and later instantiated" do
    k = Class.new(NSString)
    a = k.new
    a.class.should == k
    a.size.should == 0
    lambda { a << 'foo' }.should raise_error(RuntimeError)
  end
end

describe "An String object" do
  it "is an instance of the String/NSMutableString class" do
    ''.class.should == String
    ''.kind_of?(String).should == true
    ''.instance_of?(String).should == true
  end

  it "is mutable" do
    a = ''
    a << 'foo'
    a.should == 'foo'
  end

  it "can have a singleton class" do
    a = ''
    def a.foo; 42; end
    a.foo.should == 42
    a << 'foo'
    a.should == 'foo'
  end
end

describe "An NSString object" do
  it "is an instance of the NSString class" do
    a = NSString.string
    a.class.should == NSString
  end

  it "is immutable" do
    a = NSString.string
    a.size.should == 0
    lambda { a << 'foo' }.should raise_error(RuntimeError)
  end

  it "can have a singleton class" do
    a = NSString.string
    def a.foo; 42; end
    a.foo.should == 42
    lambda { a << 'foo' }.should raise_error(RuntimeError)
  end
end
