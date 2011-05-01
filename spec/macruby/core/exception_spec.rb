require File.dirname(__FILE__) + "/../spec_helper"
FixtureCompiler.require! "exception"
TestException # force dynamic load

describe "An Objective-C exception" do
  it "can be catched from Ruby" do
    lambda { TestException.raiseObjCException }.should raise_error
  end
end

describe "A Ruby exception" do
  it "can be catched from Objective-C" do
    o = Object.new
    def o.raiseRubyException
      raise 'foo'
    end
    TestException.catchRubyException(o).should == 1
    $!.should == nil
  end
end
