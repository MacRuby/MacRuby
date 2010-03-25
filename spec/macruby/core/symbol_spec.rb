require File.dirname(__FILE__) + "/../spec_helper"

describe "A Symbol" do
  it "should conform to the NSCoding protocol" do
    # framework 'Foundation'
    NSKeyedUnarchiver.unarchiveObjectWithData(NSKeyedArchiver.archivedDataWithRootObject(:test)).should == :test
  end

  it "should return 'Symbol' on -classForKeyedArchiver" do
    :sym.classForKeyedArchiver.class.should == Symbol.class
  end

  it "should return self on -copy" do
    :sym.copy.__id__.should == :sym.__id__
  end
end
