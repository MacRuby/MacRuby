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
    # TODO
    #lambda { a << 'foo' }.should raise_error(RuntimeError)
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

  it "can match() a Regex" do
    a = 'aaba'
    a.should match(/a+b./)
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
    # TODO
    #lambda { a << 'foo' }.should raise_error(RuntimeError)
  end
end

describe "Objective-C String methods" do
  before :each do
    @a = "test"
  end

  it "should be able to be aliased to other selectors" do
    class << @a
      alias :foo :length
    end

    @a.foo.should == @a.length
  end

  it "should be able to be aliased by pure Ruby methods" do
    class << @a
      def foo
        return 42
      end
      alias :length :foo
    end

    @a.length.should == 42
  end

  it "should be commutative when aliased" do
    class << @a
      def foo
        return 42
      end
      def do_alias
        alias :old_length :length
        alias :length :foo
      end
      def undo_alias
        alias :length :old_length
      end
    end

    @a.length.should == 4
    @a.do_alias
    @a.length.should == 42
    @a.undo_alias
    @a.length.should == 4
  end
end
