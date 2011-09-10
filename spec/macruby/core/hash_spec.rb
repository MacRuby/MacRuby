require File.dirname(__FILE__) + "/../spec_helper"

describe "The Hash class" do
  it "is a direct subclass of NSMutableDictionary" do
    Hash.class.should == Class
    Hash.superclass.should == NSMutableDictionary
  end

  it "can be subclassed and later instantiated" do
    k = Class.new(Hash)
    a = k.new
    a.class.should == k
    a[42] = 123
    a[42].should == 123
  end
end

describe "An Hash object" do
  it "is an instance of the Hash class" do
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

  it "can have a singleton class with an attr_accessor" do
    a = {}
    class << a
      attr_accessor :foo
    end
    a.foo = 42
    a.foo.should == 42
  end

  it "clones instance variables to new copy on #dup" do
    class CustomHash < Hash
      attr_accessor :foo
    end
    my_hash = CustomHash.new
    my_hash.foo = :bar

    new_hash = my_hash.dup
    new_hash.foo.should == :bar
  end

  it "properly hashes pure Cocoa objects" do
    a = NSCalendarDate.dateWithYear(2010, month:5, day:2, hour:0,  minute:0, second:0, timeZone:nil)
    b = NSCalendarDate.dateWithYear(2010, month:5, day:2, hour:0,  minute:0, second:0, timeZone:nil)
    h = {}
    h[a] = :bad
    h[b] = :ok
    h.size.should == 1
    h[a].should == :ok
  end
end

describe "An NSDictionary object" do
  before(:all) do
    require 'yaml'
  end

  it "is an instance of the Hash class" do
    a = NSDictionary.dictionary
    a.is_a?(Hash).should == true
    a.class.should == Hash
    a = NSDictionary.dictionaryWithObject(42, forKey:42)
    a.is_a?(Hash).should == true
    a.class.should == Hash
  end

  it "is immutable" do
    a = NSDictionary.dictionary
    a.size.should == 0
    lambda { a[42] = 123 }.should raise_error(RuntimeError)
  end

  it "can be transformed to yaml using #to_yaml" do
    NSDictionary.dictionaryWithDictionary({:a => "ok", :c => 42}).to_yaml.should == "--- \n:a: ok\n:c: 42\n"
  end

  it "can include Foundation objects and be correctly transformed to yaml" do
    a = NSString.stringWithString("a")
    ok = NSString.stringWithString("ok")
    ary = NSArray.arrayWithArray([42, 21])
    NSDictionary.dictionaryWithDictionary({a => ok, :c => ary}).to_yaml.should == "--- \na: ok\n:c:\n- 42\n- 21\n"
  end
end
