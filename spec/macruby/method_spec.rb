require File.dirname(__FILE__) + '/../spec_helper'

describe "A pure MacRuby method" do
  before :each do
    @o = Object.new
  end

  it "uses argument-names + colon + variable syntax to form the method name" do
    def @o.doSomething(x, withObject:y); x + y; end

    @o.should have_method(:'doSomething:withObject:')
    @o.should_not have_method(:'doSomething')
  end

  it "can have multiple arguments with the same name" do
    def @o.doSomething(x, withObject:y, withObject:z); x + y + z; end

    @o.should have_method(:'doSomething:withObject:withObject:')
    @o.should_not have_method(:'doSomething')
  end

  it "can coexist with other selectors whose first named argument(s) is/are the same" do
    def @o.foo(x); x; end
    def @o.foo(x, withObject:y); x + y; end
    def @o.foo(x, withObject:y, withObject:z); x + y + z; end

    @o.should have_method(:'foo')
    @o.should have_method(:'foo:withObject:')
    @o.should have_method(:'foo:withObject:withObject:')
  end

  it "must start with a regular argument variable followed by argument-names" do
    lambda { eval("def foo(x:y); end") }.should raise_error(SyntaxError)
    lambda { eval("def foo(x, y, with:z); end") }.should raise_error(SyntaxError)
    lambda { eval("def foo(x, with:y, z); end") }.should raise_error(SyntaxError)
  end

  it "can be called using argument-names + colon + variable syntax" do
    def @o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    @o.doSomething(30, withObject:10, withObject:2).should == 42
  end

  it "can be called using argument-name-as-symbols + => + variable syntax" do
    def @o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    @o.doSomething(30, :withObject => 10, :withObject => 2).should == 42
  end

  it "can be called mixing both syntaxes" do
    def @o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    @o.doSomething(30, withObject:10, :withObject => 2).should == 42
  end

  it "can be called using #send" do
    def @o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    @o.send(:'doSomething:withObject:withObject:', 30, 10, 2).should == 42
  end

=begin # TODO
  it "can be called using -[NSObject performSelector:]" do
    def @o.doSomething; 42; end
    @o.performSelector(:'doSomething').should == 42
  end

  it "can be called using -[NSObject performSelector:withObject:]" do
    def @o.doSomething(x); x; end
    @o.performSelector(:'doSomething:', withObject:42).should == 42
  end

  it "can be called using -[NSObject performSelector:withObject:withObject:]" do
    def @o.doSomething(x, withObject:y); x + y; end
    @o.performSelector(:'doSomething:withObject:',
                       withObject:40, withObject:2).should == 42
  end
=end

  it "cannot be called with #foo=, even if it matches the Objective-C #setFoo pattern" do
    def @o.setFoo(x); end
    @o.should_not have_method(:'foo=')
  end

  it "cannot be called with #foo?, even if it matches the Objective-C #isFoo pattern" do
    def @o.isFoo; end
    @o.should_not have_method(:'foo?')
  end

  # TODO add overloading specs
end
