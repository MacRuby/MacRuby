require File.dirname(__FILE__) + "/../spec_helper"
FixtureCompiler.require! "object"

require File.join(FIXTURES, 'object')

describe "A pure MacRuby Class" do
=begin # TODO
  it "can be instantiated from Objective-C, using +[new]" do
    o = TestObject.testNewObject(TestObjectPureMacRuby)
    o.class.should == TestObjectPureMacRuby
    o.initialized?.should == true
  end
=end

  it "can be instantiated from Objective-C, using +[alloc] and -[init]" do
    o = TestObject.testAllocInitObject(TestObjectPureMacRuby)
    o.class.should == TestObjectPureMacRuby
    o.initialized?.should == true
  end

  it "can be instantiated from Objective-C, using +[allocWithZone:] and -[init]" do
    o = TestObject.testAllocWithZoneInitObject(TestObjectPureMacRuby)
    o.class.should == TestObjectPureMacRuby
    o.initialized?.should == true
  end
end
