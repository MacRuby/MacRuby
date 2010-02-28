require File.dirname(__FILE__) + "/../spec_helper"
require File.expand_path('../../../frozen/core/object/shared/dup_clone', __FILE__)

FixtureCompiler.require! "object"
TestObject # force dynamic load
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

# Atm we don't actually do anything with the given zone, so the current
# implementation behaves exactly like #dup. Hence, we can reuse the existing
# rubyspec specs for Object#dup.
#
# But since #dup does not take an argument, we stub the spec classes to call
# #copyWithZone from a method that doesn't take an argument either.
module ObjectSpecCopyWithZone
  def copyWithZoneWithoutActualZone
    copyWithZone(nil)
  end
end
ObjectSpecDup.send(:include, ObjectSpecCopyWithZone)
ObjectSpecDupInitCopy.send(:include, ObjectSpecCopyWithZone)

describe "Object#copyWithZone:" do
  it_behaves_like :object_dup_clone, :copyWithZoneWithoutActualZone

  it "does not preserve frozen state from the original" do
    o = ObjectSpecDupInitCopy.new
    o.freeze
    o2 = o.copyWithZone(nil)

    o2.frozen?.should == false
  end
end