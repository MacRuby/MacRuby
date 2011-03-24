require File.dirname(__FILE__) + "/../spec_helper"

describe "Kernel#method_missing, the selector passed as the method argument" do
  before :all do
    @obj = Object.new
    def @obj.method_missing(m, *a, &b)
      m
    end
  end
  
  it "has the last colon stripped off if it's the only colon in the selector" do
    @obj.foo.should == :foo
    @obj.foo('bar').should == :foo
    @obj.foo('bar', 'baz').should == :foo
  end
  
  it "is passed in as a full selector if it contains multiple colons" do
    @obj.foo('bar', bar:'baz').should == :'foo:bar:'
    @obj.foo('bar', bar:'baz', baz:'bla').should == :'foo:bar:baz:'
  end
end