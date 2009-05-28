require File.dirname(__FILE__) + "/../spec_helper"
FixtureCompiler.require! "method"

require File.join(FIXTURES, 'method')

describe "A pure Objective-C method" do
  before :each do
    @o = TestMethod.new
  end

  it "can be called with #foo= if it matches the #setFoo pattern" do
    @o.should respond_to(:'setFoo')
    @o.should respond_to(:'foo=')

    @o.setFoo(123)
    @o.foo.should == 123
    @o.foo = 456
    @o.foo.should == 456
  end

  it "can be called with #foo? if it matches the #isFoo pattern" do
    @o.should respond_to(:'isFoo')
    @o.should respond_to(:'foo?')

    @o.foo?.should equal(@o.isFoo)
  end

  it "is only exposed in #methods if the second argument is true" do
    @o.methods.should_not include(:'performSelector')
    @o.methods(true).should_not include(:'performSelector')
    @o.methods(true, true).should include(:'performSelector')
  end

  it "can be called on an immediate object" do
    123.self.should == 123
    true.self.should == true
    false.self.should == false
    nil.self.should == nil
  end

  it "returning void returns nil in Ruby" do
    @o.methodReturningVoid.should == nil
  end

  it "returning nil returns nil in Ruby" do
    @o.methodReturningNil.should == nil
  end

  it "returning self returns the same receiver object" do
    @o.methodReturningSelf.should == @o
    @o.methodReturningSelf.object_id == @o.object_id
  end

  it "returning kCFBooleanTrue returns true in Ruby" do
    @o.methodReturningCFTrue.should == true
    @o.methodReturningCFTrue.class.should == TrueClass
  end

  it "returning kCFBooleanFalse returns false in Ruby" do
    @o.methodReturningCFFalse.should == false
    @o.methodReturningCFFalse.class.should == FalseClass
  end

  it "returning kCFNull returns nil in Ruby" do
    @o.methodReturningCFNull.should == nil
    @o.methodReturningCFNull.class.should == NilClass
  end

  it "returning YES returns true in Ruby" do
    @o.methodReturningYES.should == true
  end

  it "returning NO returns true in Ruby" do
    @o.methodReturningNO.should == false
  end

  it "returning 'char' or 'unsigned char' returns a Fixnum in Ruby" do
    @o.methodReturningChar.should == 42
    @o.methodReturningChar2.should == -42
    @o.methodReturningUnsignedChar.should == 42
  end
 
  it "returning 'short' or 'unsigned short' returns a Fixnum in Ruby" do
    @o.methodReturningShort.should == 42
    @o.methodReturningShort2.should == -42
    @o.methodReturningUnsignedShort.should == 42
  end

  it "returning 'int' or 'unsigned int' returns a Fixnum in Ruby" do
    @o.methodReturningInt.should == 42
    @o.methodReturningInt2.should == -42
    @o.methodReturningUnsignedInt.should == 42
  end

  it "returning 'long' or 'unsigned long' returns a Fixnum if possible in Ruby" do
    @o.methodReturningLong.should == 42
    @o.methodReturningLong2.should == -42
    @o.methodReturningUnsignedLong.should == 42
  end

  it "returning 'long' or 'unsigned long' returns a Bignum if it cannot fix in a Fixnum in Ruby" do
    @o.methodReturningLong3.should ==
      (RUBY_ARCH == 'x86_64' ? 4611686018427387904 : 1073741824)
    @o.methodReturningLong3.class.should == Bignum
    @o.methodReturningLong4.should ==
      (RUBY_ARCH == 'x86_64' ? -4611686018427387905 : -1073741825)
    @o.methodReturningLong4.class.should == Bignum
    @o.methodReturningUnsignedLong2.should ==
      (RUBY_ARCH == 'x86_64' ? 4611686018427387904 : 1073741824)
    @o.methodReturningUnsignedLong2.class.should == Bignum
  end

  it "returning 'float' returns a Float in Ruby" do
    @o.methodReturningFloat.should be_close(3.1415, 0.0001)
    @o.methodReturningFloat.class.should == Float
  end

  it "returning 'double' returns a Float in Ruby" do
    @o.methodReturningDouble.should be_close(3.1415, 0.0001)
    @o.methodReturningDouble.class.should == Float
  end

  it "returning 'SEL' returns a Symbol or nil in Ruby" do
    @o.methodReturningSEL.class.should == Symbol
    @o.methodReturningSEL.should == :'foo:with:with:'
    @o.methodReturningSEL2.class.should == NilClass
    @o.methodReturningSEL2.should == nil
  end

  it "returning 'char *' returns a String or nil in Ruby" do
    @o.methodReturningCharPtr.class.should == String
    @o.methodReturningCharPtr.should == 'foo'
    @o.methodReturningCharPtr2.class.should == NilClass
    @o.methodReturningCharPtr2.should == nil
  end

  it "returning 'NSPoint' returns an NSPoint boxed object in Ruby" do
    b = @o.methodReturningNSPoint
    b.class.should == NSPoint
    b.x.class.should == Float
    b.x.should == 1.0
    b.y.class.should == Float
    b.y.should == 2.0
  end

  it "returning 'NSSize' returns an NSSize boxed object in Ruby" do
    b = @o.methodReturningNSSize
    b.class.should == NSSize
    b.width.class.should == Float
    b.width.should == 3.0
    b.height.class.should == Float
    b.height.should == 4.0
  end

  it "returning 'NSRect' returns an NSRect boxed object in Ruby" do
    b = @o.methodReturningNSRect
    b.class.should == NSRect
    b.origin.class.should == NSPoint
    b.origin.x.should == 1.0
    b.origin.y.should == 2.0
    b.size.class.should == NSSize
    b.size.width.should == 3.0
    b.size.height.should == 4.0
  end

  it "returning 'NSRange' returns an NSRange boxed object in Ruby" do
    b = @o.methodReturningNSRange
    b.class.should == NSRange
    b.location.class.should == Fixnum
    b.location.should == 0
    b.length.class.should == Fixnum
    b.length.should == 42
  end

  it "accepting the receiver as 'id' should receive the exact same object" do
    @o.methodAcceptingSelf(@o).should == 1
  end

  it "accepting the receiver's class as 'id' should receive the exact same object" do
    @o.methodAcceptingSelfClass(@o.class).should == 1
  end

  it "accepting 'nil' as 'id' should receive Objective-C's nil" do
    @o.methodAcceptingNil(nil).should == 1
  end

  it "accepting 'true' as 'id; should receive CF's kCFBooleanTrue" do
    @o.methodAcceptingTrue(true).should == 1
  end

  it "accepting 'false' as 'id' should receive CF's kCFBooleanFalse" do
    @o.methodAcceptingFalse(false).should == 1
  end

  it "accepting a Fixnum as 'id' should receive a Fixnum boxed object" do
    @o.methodAcceptingFixnum(42).should == 1
  end

  it "accepting nil or false as 'BOOL' should receive NO, any other object should receive YES" do
    @o.methodAcceptingFalseBOOL(nil).should == 1
    @o.methodAcceptingFalseBOOL(false).should == 1

    @o.methodAcceptingTrueBOOL(true).should == 1
    @o.methodAcceptingTrueBOOL(0).should == 1
    @o.methodAcceptingTrueBOOL(123).should == 1
    @o.methodAcceptingTrueBOOL('foo').should == 1
    @o.methodAcceptingTrueBOOL(Object.new).should == 1
  end

  it "accepts a Fixnum-compatible object for an argument of types: 'char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned int', 'long', 'unsigned long'" do
    @o.methodAcceptingChar(42).should == 1
    @o.methodAcceptingUnsignedChar(42).should == 1
    @o.methodAcceptingShort(42).should == 1
    @o.methodAcceptingUnsignedShort(42).should == 1
    @o.methodAcceptingInt(42).should == 1
    @o.methodAcceptingUnsignedInt(42).should == 1
    @o.methodAcceptingLong(42).should == 1
    @o.methodAcceptingUnsignedLong(42).should == 1

    @o.methodAcceptingChar(42.0).should == 1
    @o.methodAcceptingUnsignedChar(42.0).should == 1
    @o.methodAcceptingShort(42.0).should == 1
    @o.methodAcceptingUnsignedShort(42.0).should == 1
    @o.methodAcceptingInt(42.0).should == 1
    @o.methodAcceptingUnsignedInt(42.0).should == 1
    @o.methodAcceptingLong(42.0).should == 1
    @o.methodAcceptingUnsignedLong(42.0).should == 1
  end

  it "raises a TypeError if an object is given which cannot be coerced to a Fixnum-compatible object for an argument of types: 'char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned int', 'long', 'unsigned long'" do
    lambda { @o.methodAcceptingChar(nil) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingUnsignedChar(nil) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingShort(nil) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingUnsignedShort(nil) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingInt(nil) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingUnsignedInt(nil) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingLong(nil) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingUnsignedLong(nil) }.should raise_error(TypeError)
    
    lambda { @o.methodAcceptingChar(Object.new) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingUnsignedChar(Object.new) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingShort(Object.new) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingUnsignedShort(Object.new) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingInt(Object.new) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingUnsignedInt(Object.new) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingLong(Object.new) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingUnsignedLong(Object.new) }.should raise_error(TypeError)
  end

  it "automatically coerces an object with #to_i for an argument of types: 'char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned int', 'long', 'unsigned long'" do
    o2 = Object.new
    def o2.to_i; 42; end

    @o.methodAcceptingChar(o2).should == 1
    @o.methodAcceptingUnsignedChar(o2).should == 1
    @o.methodAcceptingShort(o2).should == 1
    @o.methodAcceptingUnsignedShort(o2).should == 1
    @o.methodAcceptingInt(o2).should == 1
    @o.methodAcceptingUnsignedInt(o2).should == 1
    @o.methodAcceptingLong(o2).should == 1
    @o.methodAcceptingUnsignedLong(o2).should == 1
  end

  it "accepting a one-character string as 'char' or 'unsigned char' should receive the first character" do
    @o.methodAcceptingChar('*').should == 1
    @o.methodAcceptingUnsignedChar('*').should == 1
  end

  it "accepting a String, Symbol or nil as 'SEL' should receive the appropriate selector" do
    @o.methodAcceptingSEL(:'foo:with:with:').should == 1
    @o.methodAcceptingSEL('foo:with:with:').should == 1
    @o.methodAcceptingSEL2(nil).should == 1

    lambda { @o.methodAcceptingSEL(123) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingSEL(Object.new) }.should raise_error(TypeError)
  end

  it "accepting a Float-compatible object as 'float' or 'double' should receive the appropriate data" do
    @o.methodAcceptingFloat(3.1415).should == 1 
    @o.methodAcceptingDouble(3.1415).should == 1

    o2 = Object.new
    def o2.to_f; 3.1415; end

    @o.methodAcceptingFloat(o2).should == 1 
    @o.methodAcceptingDouble(o2).should == 1

    lambda { @o.methodAcceptingFloat(nil) }.should raise_error(TypeError) 
    lambda { @o.methodAcceptingDouble(nil) }.should raise_error(TypeError) 
    lambda { @o.methodAcceptingFloat(Object.new) }.should raise_error(TypeError) 
    lambda { @o.methodAcceptingDouble(Object.new) }.should raise_error(TypeError) 
  end

  it "accepting a String-compatible object as 'char *' should receive the appropriate data" do
    @o.methodAcceptingCharPtr('foo').should == 1

    o2 = Object.new
    def o2.to_str; 'foo' end

    @o.methodAcceptingCharPtr(o2).should == 1

    lambda { @o.methodAcceptingCharPtr(123) }.should raise_error(TypeError) 
    lambda { @o.methodAcceptingCharPtr([]) }.should raise_error(TypeError) 
    lambda { @o.methodAcceptingCharPtr(Object.new) }.should raise_error(TypeError) 

    @o.methodAcceptingCharPtr2(nil).should == 1
  end

  it "accepting an NSPoint, NSSize, NSRange or NSRect object as 'NSPoint', 'NSSize', 'NSRange' or 'NSRect' should receive the C structure" do
    p = @o.methodReturningNSPoint
    @o.methodAcceptingNSPoint(p).should == 1
    p = @o.methodReturningNSSize
    @o.methodAcceptingNSSize(p).should == 1
    p = @o.methodReturningNSRect
    @o.methodAcceptingNSRect(p).should == 1
    p = @o.methodReturningNSRange
    @o.methodAcceptingNSRange(p).should == 1

    lambda { @o.methodAcceptingNSPoint(nil) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSPoint(123) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSPoint(Object.new) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSPoint(@o.methodReturningNSSize) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSSize(nil) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSSize(123) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSSize(Object.new) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSSize(@o.methodReturningNSPoint) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSRect(nil) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSRect(123) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSRect(Object.new) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSRect(@o.methodReturningNSPoint) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSRange(nil) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSRange(123) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSRange(Object.new) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSRange(@o.methodReturningNSPoint) }.should raise_error(TypeError)
  end

  it "accepting an Array of valid objects as a structure type should receive the C structure" do
    @o.methodAcceptingNSPoint([1, 2]).should == 1
    @o.methodAcceptingNSSize([3, 4]).should == 1
    @o.methodAcceptingNSRect([[1, 2], [3, 4]]).should == 1
    @o.methodAcceptingNSRect([1, 2, 3, 4]).should == 1
    @o.methodAcceptingNSRange([0, 42]).should == 1

    lambda { @o.methodAcceptingNSPoint([1]) }.should raise_error(ArgumentError)
    lambda { @o.methodAcceptingNSPoint([1, 2, 3]) }.should raise_error(ArgumentError)
    lambda { @o.methodAcceptingNSRect([1, 2, 3]) }.should raise_error(ArgumentError)
    lambda { @o.methodAcceptingNSRect([1, 2, 3, 4, 5]) }.should raise_error(ArgumentError)
    lambda { @o.methodAcceptingNSRect([[1, 2], [3]]) }.should raise_error(ArgumentError)
    lambda { @o.methodAcceptingNSRect([[1, 2], [3, 4, 5]]) }.should raise_error(ArgumentError)
  end

  it "accepting various C types should receive these types as expected" do
    @o.methodAcceptingInt(42, float:42, double:42, short:42, NSPoint:[42, 42],
                          NSRect:[42, 42, 42, 42], char:42).should == 1
  end

  it "accepting an 'int *' should be given a Pointer object and receive the pointer as expected" do
    p = Pointer.new(:int)
    p[0] = 42
    @o.methodAcceptingIntPtr(p).should == 1
    p[0].should == 43

    lambda { @o.methodAcceptingIntPtr(Pointer.new(:uint)) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingIntPtr(123) }.should raise_error(TypeError)

    @o.methodAcceptingIntPtr2(nil).should == 1

    lambda { @o.methodAcceptingIntPtr2(Pointer.new(:uint)) }.should raise_error(TypeError)
  end

  it "accepting an 'id *' should be given a Pointer object and receive the pointer as expected" do
    p = Pointer.new(:object)
    p[0] = @o
    @o.methodAcceptingObjectPtr(p).should == 1
    p[0].should == NSObject

    lambda { @o.methodAcceptingObjectPtr(Pointer.new(:int)) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingObjectPtr(123) }.should raise_error(TypeError)

    @o.methodAcceptingObjectPtr2(nil).should == 1

    lambda { @o.methodAcceptingObjectPtr2(Pointer.new(:int)) }.should raise_error(TypeError)
  end

  it "accepting an 'NSRect *' should be given a Pointer object and receive the pointer as expected" do
    p = Pointer.new(NSRect.type)
    p[0] = [1, 2, 3, 4]
    @o.methodAcceptingNSRectPtr(p).should == 1
    p[0].origin.x.should == 42
    p[0].origin.y.should == 43
    p[0].size.width.should == 44
    p[0].size.height.should == 45

    lambda { @o.methodAcceptingNSRectPtr(Pointer.new(NSPoint.type)) }.should raise_error(TypeError)
    lambda { @o.methodAcceptingNSRectPtr(123) }.should raise_error(TypeError)

    @o.methodAcceptingNSRectPtr2(nil).should == 1

    lambda { @o.methodAcceptingNSRectPtr2(Pointer.new(NSPoint.type)) }.should raise_error(TypeError)
  end
end

describe "A pure MacRuby method" do
  before :each do
    @o = TestMethodOverride.new
  end

  it "can overwrite an Objective-C method returning void" do
    @o.methodReturningVoid.should == 42
    TestMethodOverride.testMethodReturningVoid(@o).should == 1
  end

  it "can overwrite an Objective-C method returning self" do
    @o.methodReturningSelf.should == @o
    TestMethodOverride.testMethodReturningSelf(@o).should == 1
  end

  it "can overwrite an Objective-C method returning nil as 'id'" do
    @o.methodReturningNil.should == nil
    TestMethodOverride.testMethodReturningNil(@o).should == 1
  end

  it "can overwrite an Objective-C method returning kCFBooleanTrue as 'id'" do
    @o.methodReturningCFTrue.should == true
    TestMethodOverride.testMethodReturningCFTrue(@o).should == 1
  end

  it "can overwrite an Objective-C method returning kCFBooleanFalse as 'id'" do
    @o.methodReturningCFFalse.should == false
    TestMethodOverride.testMethodReturningCFFalse(@o).should == 1
  end

  it "can overwrite an Objective-C method returning YES as 'BOOL'" do
    @o.methodReturningYES.should == true
    TestMethodOverride.testMethodReturningYES(@o).should == 1
  end

  it "can overwrite an Objective-C method returning NO as 'BOOL'" do
    @o.methodReturningNO.should == false
    TestMethodOverride.testMethodReturningNO(@o).should == 1
  end

  it "can overwrite an Objective-C method returning 'unsigned char' or 'char'" do
    @o.methodReturningChar.should == 42
    @o.methodReturningChar2.should == -42
    @o.methodReturningUnsignedChar.should == 42
    TestMethodOverride.testMethodReturningChar(@o).should == 1
    TestMethodOverride.testMethodReturningChar2(@o).should == 1
    TestMethodOverride.testMethodReturningUnsignedChar(@o).should == 1
  end

  it "can overwrite an Objective-C method returning 'unsigned short' or 'short'" do
    @o.methodReturningShort.should == 42
    @o.methodReturningShort2.should == -42
    @o.methodReturningUnsignedShort.should == 42
    TestMethodOverride.testMethodReturningShort(@o).should == 1
    TestMethodOverride.testMethodReturningShort2(@o).should == 1
    TestMethodOverride.testMethodReturningUnsignedShort(@o).should == 1
  end

  it "can overwrite an Objective-C method returning 'unsigned int' or 'int'" do
    @o.methodReturningInt.should == 42
    @o.methodReturningInt2.should == -42
    @o.methodReturningUnsignedInt.should == 42
    TestMethodOverride.testMethodReturningInt(@o).should == 1
    TestMethodOverride.testMethodReturningInt2(@o).should == 1
    TestMethodOverride.testMethodReturningUnsignedInt(@o).should == 1
  end

  it "can overwrite an Objective-C method returning 'unsigned long' or 'long'" do
    @o.methodReturningLong.should == 42
    @o.methodReturningLong2.should == -42
    @o.methodReturningUnsignedLong.should == 42
    TestMethodOverride.testMethodReturningLong(@o).should == 1
    TestMethodOverride.testMethodReturningLong2(@o).should == 1
    TestMethodOverride.testMethodReturningUnsignedLong(@o).should == 1
  end

  it "can overwrite an Objective-C method returning 'float' or 'double'" do
    @o.methodReturningFloat.should == 3.1415
    @o.methodReturningDouble.should == 3.1415
    TestMethodOverride.testMethodReturningFloat(@o).should == 1
    TestMethodOverride.testMethodReturningDouble(@o).should == 1
  end

  it "can overwrite an Objective-C method returning 'SEL'" do
    @o.methodReturningSEL.should == :'foo:with:with:'
    @o.methodReturningSEL2.should == nil
    TestMethodOverride.testMethodReturningSEL(@o).should == 1
    TestMethodOverride.testMethodReturningSEL2(@o).should == 1
  end

  it "can overwrite an Objective-C method returning 'char *'" do
    @o.methodReturningCharPtr.should == 'foo'
    @o.methodReturningCharPtr2.should == nil
    TestMethodOverride.testMethodReturningCharPtr(@o).should == 1
    TestMethodOverride.testMethodReturningCharPtr2(@o).should == 1
  end

  it "can overwrite an Objective-C method returning 'NSPoint'" do
    @o.methodReturningNSPoint.should == NSPoint.new(1, 2)
    TestMethodOverride.testMethodReturningNSPoint(@o).should == 1
  end

  it "can overwrite an Objective-C method returning 'NSSize'" do
    @o.methodReturningNSSize.should == NSSize.new(3, 4)
    TestMethodOverride.testMethodReturningNSSize(@o).should == 1
  end

  it "can overwrite an Objective-C method returning 'NSRect'" do
    @o.methodReturningNSRect.should == NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4))
    TestMethodOverride.testMethodReturningNSRect(@o).should == 1
  end

  it "can overwrite an Objective-C method returning 'NSRange'" do
    @o.methodReturningNSRange.should == NSRange.new(0, 42)
    TestMethodOverride.testMethodReturningNSRange(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting self" do
    @o.methodAcceptingSelf(@o).should == 1
    TestMethodOverride.testMethodAcceptingSelf(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting self class" do
    @o.methodAcceptingSelfClass(@o.class).should == 1
    TestMethodOverride.testMethodAcceptingSelfClass(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting nil" do
    @o.methodAcceptingNil(nil).should == 1
    TestMethodOverride.testMethodAcceptingNil(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting true/false (as id)" do
    @o.methodAcceptingTrue(true).should == 1
    @o.methodAcceptingFalse(false).should == 1
    TestMethodOverride.testMethodAcceptingTrue(@o).should == 1
    TestMethodOverride.testMethodAcceptingFalse(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting a fixnum (as id)" do
    @o.methodAcceptingFixnum(42).should == 1
    TestMethodOverride.testMethodAcceptingFixnum(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'char' or 'unsigned char'" do
    @o.methodAcceptingChar(42).should == 1
    @o.methodAcceptingUnsignedChar(42).should == 1
    TestMethodOverride.testMethodAcceptingChar(@o).should == 1
    TestMethodOverride.testMethodAcceptingUnsignedChar(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'short' or 'unsigned short'" do
    @o.methodAcceptingShort(42).should == 1
    @o.methodAcceptingUnsignedShort(42).should == 1
    TestMethodOverride.testMethodAcceptingShort(@o).should == 1
    TestMethodOverride.testMethodAcceptingUnsignedShort(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'int' or 'unsigned int'" do
    @o.methodAcceptingInt(42).should == 1
    @o.methodAcceptingUnsignedInt(42).should == 1
    TestMethodOverride.testMethodAcceptingInt(@o).should == 1
    TestMethodOverride.testMethodAcceptingUnsignedInt(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'long' or 'unsigned long'" do
    @o.methodAcceptingLong(42).should == 1
    @o.methodAcceptingUnsignedLong(42).should == 1
    TestMethodOverride.testMethodAcceptingLong(@o).should == 1
    TestMethodOverride.testMethodAcceptingUnsignedLong(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting true/false (as BOOL)" do
    @o.methodAcceptingTrueBOOL(true).should == 1
    @o.methodAcceptingFalseBOOL(false).should == 1
    TestMethodOverride.testMethodAcceptingTrueBOOL(@o).should == 1
    TestMethodOverride.testMethodAcceptingFalseBOOL(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'SEL'" do
    @o.methodAcceptingSEL(:'foo:with:with:').should == 1
    @o.methodAcceptingSEL2(nil).should == 1
    TestMethodOverride.testMethodAcceptingSEL(@o).should == 1
    TestMethodOverride.testMethodAcceptingSEL2(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'char *'" do
    @o.methodAcceptingCharPtr('foo').should == 1
    @o.methodAcceptingCharPtr2(nil).should == 1
    TestMethodOverride.testMethodAcceptingCharPtr(@o).should == 1
    TestMethodOverride.testMethodAcceptingCharPtr2(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'float'" do
    @o.methodAcceptingFloat(3.1415).should == 1
    TestMethodOverride.testMethodAcceptingFloat(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'double'" do
    @o.methodAcceptingDouble(3.1415).should == 1
    TestMethodOverride.testMethodAcceptingDouble(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'NSPoint'" do
    @o.methodAcceptingNSPoint(NSPoint.new(1, 2)).should == 1
    TestMethodOverride.testMethodAcceptingNSPoint(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'NSSize'" do
    @o.methodAcceptingNSSize(NSSize.new(3, 4)).should == 1
    TestMethodOverride.testMethodAcceptingNSSize(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'NSRect'" do
    @o.methodAcceptingNSRect(NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4))).should == 1
    TestMethodOverride.testMethodAcceptingNSRect(@o).should == 1
  end

  it "can overwrite an Objective-C method accepting 'NSRange'" do
    @o.methodAcceptingNSRange(NSRange.new(0, 42)).should == 1
    TestMethodOverride.testMethodAcceptingNSRange(@o).should == 1
  end

  it "can overwrite a complex Objective-C method" do
    @o.methodAcceptingInt(42, float:42, double:42, short:42, NSPoint:NSPoint.new(42, 42), NSRect:NSRect.new(NSPoint.new(42, 42), NSSize.new(42, 42)), char:42).should == 1
    TestMethodOverride.testMethodAcceptingComplexTypes(@o).should == 1
  end
end

describe "A pure MacRuby method" do
  before :each do
    @o = TestInformalProtocolMethod.new
  end
  
  it "whose selector matches an informal protocol is defined on the Objective-C side with the correct type encoding" do
    TestMethod.testInformalProtocolMethod1(@o).should == 1 
    TestMethod.testInformalProtocolMethod2(@o).should == 1 
  end
end
