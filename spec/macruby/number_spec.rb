require File.dirname(__FILE__) + "/spec_helper"

describe "An NSNumber boolean object" do
  it "can be compared against a true/false Ruby type" do
    true.should == NSNumber.numberWithBool(true)
    true.should != NSNumber.numberWithBool(false)
    false.should == NSNumber.numberWithBool(false)
    false.should != NSNumber.numberWithBool(true)
  end
end

# TODO cover the Numeric interface on top of NSNumber
