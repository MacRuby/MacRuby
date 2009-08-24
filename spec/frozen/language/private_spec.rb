require File.dirname(__FILE__) + '/../spec_helper'
require File.dirname(__FILE__) + '/fixtures/private'

describe "The private keyword" do
  ruby_version_is ""..."1.9" do
    it "marks following methods as being private" do
      a = Private::A.new
      a.methods.should_not include("bar")
      lambda { a.bar }.should raise_error(NoMethodError)

      b = Private::B.new
      b.methods.should_not include("bar")
      lambda { b.bar }.should raise_error(NoMethodError)
    end

    it "is overridden when a new class is opened" do
      c = Private::B::C.new
      c.methods.should include("baz")
      c.baz
      Private::B::public_class_method1.should == 1
      Private::B::public_class_method2.should == 2
      lambda { Private::B::private_class_method1 }.should raise_error(NoMethodError)
    end

    it "is no longer in effect when the class is closed" do
      b = Private::B.new
      b.methods.should include("foo")
      b.foo
    end
  end

  ruby_version_is "1.9" do
    it "marks following methods as being private" do
      a = Private::A.new
      a.methods.should_not include(:bar)
      lambda { a.bar }.should raise_error(NoMethodError)

      b = Private::B.new
      b.methods.should_not include(:bar)
      lambda { b.bar }.should raise_error(NoMethodError)
    end

    it "is overridden when a new class is opened" do
      c = Private::B::C.new
      c.methods.should include(:baz)
      c.baz
      Private::B::public_class_method1.should == 1
      Private::B::public_class_method2.should == 2
      lambda { Private::B::private_class_method1 }.should raise_error(NoMethodError)
    end

    it "is no longer in effect when the class is closed" do
      b = Private::B.new
      b.methods.should include(:foo)
      b.foo
    end
  end

  it "changes visibility of previously called method" do
    klass = Private::F
    f = klass.new
    f.foo
    klass.class_eval { private :foo }
    lambda { f.foo }.should raise_error(NoMethodError)
  end

  it "changes visiblity of previously called methods with same send/call site" do
    klass = Private::G
    g = klass.new
    lambda {
      2.times do
        g.foo
        klass.class_eval { private :foo }
      end
    }.should raise_error(NoMethodError)
  end
  
  it "changes the visibility of the existing method in the subclass" do
    Private::A.new.foo.should == 'foo'
    lambda {Private::H.new.foo}.should raise_error(NoMethodError) 
    Private::H.new.send(:foo).should == 'foo'
  end
end
