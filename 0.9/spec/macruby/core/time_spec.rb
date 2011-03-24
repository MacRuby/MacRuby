require File.expand_path("../../spec_helper", __FILE__)

describe "The Time class" do
  it "is a direct subclass of NSDate" do
    Time.class.should == Class
    Time.superclass.should == NSDate
  end
end

describe "A Time object" do
  it "behaves like an NSDate" do
    obj = Time.at(42424242)
    obj.timeIntervalSince1970.should == 42424242
    obj2 = NSDate.dateWithTimeIntervalSince1970(42424242)
    obj.isEqualToDate(obj2).should == true
    obj2.isEqualToDate(obj).should == true
  end
end
