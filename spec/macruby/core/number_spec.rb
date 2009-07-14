require File.dirname(__FILE__) + "/../spec_helper"

describe "An NSNumber boolean object" do
  it "can be compared against a true/false Ruby type" do
    true.should == NSNumber.numberWithBool(true)
    true.should != NSNumber.numberWithBool(false)
    false.should == NSNumber.numberWithBool(false)
    false.should != NSNumber.numberWithBool(true)
  end
end

# TODO cover the Numeric interface on top of NSNumber

describe "Fixnum#popcnt" do
	it "counts the number of set bits in a number" do
		5.popcnt.should == 2
		0.popcnt.should == 0
		1024.popcnt.should == 1
		# popcnt can, and should, vary depending on machine word size for negative numbers.
		-1.popcnt.should <= 64
	end
end