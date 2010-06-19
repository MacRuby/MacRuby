require File.dirname(__FILE__) + "/../spec_helper"

describe "The Array class" do
  it "is a direct subclass of NSMutableArray" do
    Array.class.should == Class
    Array.superclass.should == NSMutableArray
  end

  it "can be subclassed and later instantiated" do
    k = Class.new(Array)
    a = k.new
    a.class.should == k
    a << 42
    a[0].should == 42
  end
end

describe "An Array object" do
  it "is an instance of the Array/NSMutableArray class" do
    [].class.should == Array
    [].kind_of?(Array).should == true
    [].instance_of?(Array).should == true
  end

  it "is mutable" do
    a = []
    a << 42
    a[0].should == 42
  end

  it "can have a singleton class" do
    a = []
    def a.foo; 42; end
    a.foo.should == 42
    a << 42
    a[0].should == 42
  end

  it "can have a singleton class with an attr_accessor" do
    a = []
    class << a
      attr_accessor :foo
    end
    a.foo = 42
    a.foo.should == 42
  end
end

describe "An NSArray object" do
  it "is an instance of the Array class" do
    a = NSArray.array
    a.class.should == Array
    a.is_a?(Array).should == true
    a = NSArray.arrayWithObject(42)
    a.class.should == Array
    a.is_a?(Array).should == true
  end

  it "is immutable" do
    a = NSArray.array
    a.size.should == 0
    lambda { a << 123 }.should raise_error(RuntimeError)
  end

  it "can be transformed to yaml using #to_yaml" do
    require 'yaml'
    NSArray.arrayWithArray([1, 2, 42]).to_yaml.should == "--- \n- 1\n- 2\n- 42\n"
  end
end

# This test exists because the previous implementation of NSArray #map etc.
# was using -mutableCopy to define the array to modify, which didn't work with
# `SBElementArray`s. Let's make sure this keeps working
describe "An SBElementArray (subclass of NSMutableArray)" do
  it "responds to #map, #shuffle, etc." do
    framework 'ScriptingBridge'
    finder = SBApplication.applicationWithBundleIdentifier('com.apple.finder')
    homeFolderItems = finder.home.items
    lambda { homeFolderItems.map { |i| i.name } }.should_not raise_error(RuntimeError)
    lambda { homeFolderItems.shuffle }.should_not raise_error(RuntimeError)
  end
end

