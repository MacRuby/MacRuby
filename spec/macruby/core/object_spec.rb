require File.dirname(__FILE__) + "/../spec_helper"
require File.expand_path('../../../frozen/core/object/shared/dup_clone', __FILE__)

FixtureCompiler.require! "object"
TestObject # force dynamic load
TestProtocolConformance
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

  it "returns whether or not it conforms to the given protocol" do
    o = TestObjectThatDoesNotCompletelyConformToProtocol
    conforms = TestProtocolConformance.checkIfObjectConformsToTestProtocol(o)
    conforms.should == 0

    o = TestObjectThatDoesNotCompletelyConformToProtocol.new
    conforms = TestProtocolConformance.checkIfObjectConformsToTestProtocol(o)
    conforms.should == 0

    o = TestObjectThatConformsToProtocol
    conforms = TestProtocolConformance.checkIfObjectConformsToTestProtocol(o)
    conforms.should == 1

    o = TestObjectThatConformsToProtocol.new
    conforms = TestProtocolConformance.checkIfObjectConformsToTestProtocol(o)
    conforms.should == 1

    o = Class.new.new
    prot = Protocol.protocolWithName(:NSCoding)
    o.conformsToProtocol(prot).should == false
    def o.initWithCoder(coder); nil; end
    o.conformsToProtocol(prot).should == false
    def o.encodeWithCoder(coder); nil; end
    o.conformsToProtocol(prot).should == true
    o.conformsToProtocol(prot).should == true # again, as it's now cached
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

describe "An Objective-C class" do
  it "has its +initialize method called once it is accessed for the first time" do
    3.times { ::TestClassInitialization.result.should == 42 }

    # If +[NSPredicate initialize] is not properly called, this will raise a warning in the
    # running terminal (see https://www.macruby.org/trac/ticket/458).
    obj = NSPredicate.predicateWithFormat("NOT (SELF in %@)", argumentArray: [[1,2,3,4]])
    obj.kind_of?(NSPredicate).should == true
  end

  it "with a custom Objective-C method resolver can still be used to add new methods from Ruby" do
    class TestCustomMethodResolverSub < ::TestCustomMethodResolver
      def foo; 42; end
      alias_method :bar, :foo 
    end
    o = TestCustomMethodResolverSub.new
    o.foo.should == 42
    o.bar.should == 42
  end

  it "has its isEqual: method used for #== and #eql?" do
    a = NSCalendarDate.dateWithYear(2010, month:5, day:2, hour:0,  minute:0, second:0, timeZone:nil)
    b = NSCalendarDate.dateWithYear(2010, month:5, day:2, hour:0,  minute:0, second:0, timeZone:nil)
    a.isEqual(b).should == true
    (a == b).should == true
    a.eql?(b).should == true
    a.equal?(b).should == false
  end

  it "uses #description when it receives #inspect" do
    url = NSURL.URLWithString('http://macruby.org/')
    url.inspect.should == url.description

    bundle = NSBundle.mainBundle
    bundle.inspect.should == bundle.description
  end
end
