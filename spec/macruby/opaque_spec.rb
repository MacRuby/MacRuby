require File.dirname(__FILE__) + '/../spec_helper'

framework 'Foundation'

describe "A BridgeSupport opaque type" do
  it "is an instance of Boxed" do
    NSZone.superclass.should == Boxed
  end

  it "cannot be created with #new" do
    lambda { NSZone.new }.should raise_error(RuntimeError)
  end

  it "can be created from an Objective-C API, and passed back to Objective-C" do
    z = 123.zone
    z.class.should == NSZone
    lambda { Object.allocWithZone(z).init }.should_not raise_error
    lambda { Object.allocWithZone(nil).init }.should_not raise_error
    lambda { Object.allocWithZone(123).init }.should raise_error(TypeError)
    lambda { Object.allocWithZone(NSPoint.new).init }.should raise_error(TypeError)
  end

  it "can be compared to an exact same instance using #==" do
    123.zone.should == 456.zone
  end

  it "returns true when the #opaque? class method is called" do
    NSZone.opaque?.should == true
  end 

  it "returns its Objective-C encoding type when then #type class method is called" do
    NSZone.type.should == '^{_NSZone=}'
  end
end
