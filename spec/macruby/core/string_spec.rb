require File.dirname(__FILE__) + "/../spec_helper"

describe "The String class" do
  it "is a direct subclass of NSMutableString" do
    String.class.should == Class
    String.superclass.should == NSMutableString
  end

  it "can be subclassed and later instantiated" do
    k = Class.new(String)
    a = k.new
    a.class.should == k
    a << 'foo'
    a.should == 'foo'
  end
end

describe "A String object" do
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

  it "can have a singleton class with an attr_accessor" do
    a = ''
    class << a
      attr_accessor :foo
    end
    a.foo = 42
    a.foo.should == 42
  end

  it "responds to #to_data and returns an NSData object wrapping the internal storage" do
    s = 'foo'.force_encoding(Encoding::ASCII)
    o = s.to_data
    o.kind_of?(NSData).should == true
    o.length.should == 3
    ptr = o.bytes
    ptr.class.should == Pointer
    ptr[0].should == 102
    ptr[1].should == 111
    ptr[2].should == 111

    s = 'はい'.force_encoding(Encoding::UTF_8)
    o = s.to_data
    o.kind_of?(NSData).should == true
    o.length.should == 6
    ptr = o.bytes
    ptr.class.should == Pointer
    ptr[0].should == 227
    ptr[1].should == 129
    ptr[2].should == 175
    ptr[3].should == 227
    ptr[4].should == 129
    ptr[5].should == 132

    s = File.read('/bin/cat').force_encoding(Encoding::BINARY)
    o = s.to_data
    o.kind_of?(NSData).should == true
    o.length.should == s.size
    s_bytes = s.bytes.to_a
    ptr = o.bytes
    ptr.class.should == Pointer
    i = 0; c = s_bytes.size
    while i < c
      ptr[i].should == s_bytes[i]
      i += 1000
    end
  end

  it "responds to #pointer which returns a Pointer wrapping the internal storage" do
    s = 'hey'
    ptr = s.pointer
    3.times do |i|
      ptr[i].chr.should == s[i]
    end
    ptr.class.should == Pointer
    s2 = NSString.alloc.initWithBytes(ptr, length: 3, encoding: NSASCIIStringEncoding)
    s2.should == s
  end

  it "responds to #transform which returns the transliterated version of the receiver" do
    ''.transform('latin-greek').should == nil

    s = 'hello'
    s.transform('latin-hiragana').should == "へっろ"
    lambda { s.transform('made-up') }.should raise_error(ArgumentError)
  end
end

describe "An NSString object" do
  it "is an instance of the String class" do
    a = NSString.string
    a.class.should == String
    a = NSString.stringWithString('OMG')
    a.class.should == String
  end

  it "is immutable" do
    a = NSString.string
    a.size.should == 0
    lambda { a << 'foo' }.should raise_error(RuntimeError)
  end

  it "forwards the block when calling a ruby method" do
    NSString.stringWithString("ybuRcaM").sub(/.+/) { |s| s.reverse }.should == "MacRuby"
  end

  it "can be transformed to yaml using #to_yaml" do
    require 'yaml'
    NSString.stringWithString("ok").to_yaml.should == "--- ok\n"
  end

  [[:bytesize, []],
   [:getbyte, [1]],
   [:setbyte, [0, 42]],
   [:force_encoding, [Encoding::ASCII]],
   [:valid_encoding?, []],
   [:ascii_only?, []],
   [:bytes, []],
   [:each_byte, []],
   [:to_data, []],
   [:pointer, []]].each do |msg, args|
    it "responds to ##{msg} but raises an exception" do
      lambda { NSString.stringWithString('test').send(msg, *args) }.should raise_error(ArgumentError)
    end
  end
end
