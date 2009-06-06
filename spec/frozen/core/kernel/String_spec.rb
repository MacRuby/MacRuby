require File.dirname(__FILE__) + '/../../spec_helper'
require File.dirname(__FILE__) + '/fixtures/classes'

describe :kernel_String, :shared => true do
  it "converts the given argument to a String by calling #to_s" do
    @object.send(@method, nil).should == ""
    @object.send(@method, 1.12).should == "1.12"
    @object.send(@method, false).should == "false"
    @object.send(@method, Object).should == "Object"

    (obj = mock('test')).should_receive(:to_s).and_return("test")
    @object.send(@method, obj).should == "test"
  end

  it "raises a TypeError if #to_s is not provided" do
    class << (obj = mock('to_s'))
      undef_method :to_s
    end

    lambda { Kernel.String(obj) }.should raise_error(TypeError)
  end
  
  it "raises a TypeError if respond_to? returns false for to_s" do
    obj = mock("to_s")
    def obj.respond_to?(meth, *)
      meth.to_s == "to_s" ? false : super
    end

    lambda { String(obj) }.should raise_error(TypeError)
  end

  it "tries to call the to_s method if respond_to? returns true for to_s" do
    class << (obj = mock('to_s'))
      undef_method :to_s
    end

    def obj.respond_to?(meth, *)
      meth.to_s == "to_s" ? true : super
    end

    lambda { @object.send(@method, obj) }.should raise_error(NoMethodError)
  end

  it "raises a TypeError if #to_s does not return a String" do
    (obj = mock('123')).should_receive(:to_s).and_return(123)
    lambda { @object.send(@method, obj) }.should raise_error(TypeError)
  end

  it "returns the same object if it already a String" do
    string = "Hello"
    string.should_not_receive(:to_s)
    string2 = @object.send(@method, string)
    string.should equal(string2)
  end

  it "returns the same object if it is an instance of a String subclass" do
    subklass = Class.new(String)
    string = subklass.new("Hello")
    string.should_not_receive(:to_s)
    string2 = @object.send(@method, string)
    string.should equal(string2)
  end
end

describe "Kernel.String" do
  it_behaves_like :kernel_String, :String, Kernel
end

describe "Kernel#String" do
  it_behaves_like :kernel_String, :String, mock("receiver for String()")

  it "is a private method" do
    Kernel.should have_private_instance_method(:String)
  end
end
