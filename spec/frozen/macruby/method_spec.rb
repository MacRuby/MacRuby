require File.dirname(__FILE__) + '/../spec_helper'

describe "A MacRuby method" do
  it "uses argument-names + colon + variable syntax to form the method name" do
    o = Object.new
    def o.doSomething(x, withObject:y); x + y; end
    o.respond_to?(:'doSomething:withObject:').should == true
    o.respond_to?(:'doSomething').should == false
  end

  it "can have multiple arguments with the same name" do
    o = Object.new
    def o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    o.respond_to?(:'doSomething:withObject:withObject:').should == true
    o.respond_to?(:'doSomething').should == false
  end

  it "can coexist with other selectors whose first part is similar" do
    o = Object.new
    def o.foo(x); x; end
    def o.foo(x, withObject:y); x + y; end
    o.respond_to?(:'foo').should == true
    o.respond_to?(:'foo:withObject:').should == true
  end

  it "must start by a regular argument variable then followed by argument-names" do
    lambda { eval("def foo(x:y); end") }.should raise_error(SyntaxError)
    lambda { eval("def foo(x, y, with:z); end") }.should raise_error(SyntaxError)
    lambda { eval("def foo(x, with:y, z); end") }.should raise_error(SyntaxError)
  end

  it "can be called using argument-names + colon + variable syntax" do
    o = Object.new
    def o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    o.doSomething(30, withObject:10, withObject:2).should == 42
  end

  it "can be called using argument-name-as-symbols + => + variable syntax" do
    o = Object.new
    def o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    o.doSomething(30, :withObject => 10, :withObject => 2).should == 42
  end

  it "can be called mixing both syntaxes" do
    o = Object.new
    def o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    o.doSomething(30, withObject:10, :withObject => 2).should == 42
  end

  it "can be called using #send" do
    o = Object.new
    def o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    o.send(:'doSomething:withObject:withObject:', 30, 10, 2).should == 42
  end

  it "can be called using -[NSObject performSelector:]" do
    o = Object.new
    def o.doSomething; 42; end
    o.performSelector(:'doSomething').should == 42
  end

  it "can be called using -[NSObject performSelector:withObject:]" do
    o = Object.new
    def o.doSomething(x); x; end
    o.performSelector(:'doSomething:', withObject:42).should == 42
  end

  it "can be called using -[NSObject performSelector:withObject:withObject:]" do
    o = Object.new
    def o.doSomething(x, withObject:y); x + y; end
    o.performSelector(:'doSomething:withObject:',
                      withObject:40, withObject:2).should == 42
  end
end

describe "An Objective-C method" do
  it "named using the setFoo pattern can be called using #foo=" do
    o = []
    o.respond_to?(:'setArray').should == true
    o.respond_to?(:'array=').should == true
    o.array = [1, 2, 3]
    o.should == [1, 2, 3]
  end

  it "named using the isFoo pattern can be called using #foo?" do
    o = NSBundle.mainBundle
    o.respond_to?(:'isLoaded').should == true
    o.respond_to?(:'loaded?').should == true
    o.loaded?.should == true
  end

  it "is only exposed in #methods if the second argument is true" do
    o = Object.new
    o.methods.include?(:'performSelector:').should == false
    o.methods(true).include?(:'performSelector:').should == false
    o.methods(false).include?(:'performSelector:').should == false
    o.methods(true, true).include?(:'performSelector:').should == true
    o.methods(false, true).include?(:'performSelector:').should == true
  end
end
