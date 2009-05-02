require File.dirname(__FILE__) + "/spec_helper"
FixtureCompiler.require! "constant"

describe "A BridgeSupport constant" do
  it "of type 'id' is available as an Object in Ruby" do
    ConstantObject.class.should == NSString
    ConstantObject.should == 'foo'
  end

  it "of type 'Class' is available as a Class in Ruby" do
    ConstantClass.class.should == Class
    ConstantClass.should == NSObject
  end

  it "of type 'SEL' is available as a Symbol in Ruby" do
    ConstantSEL.class.should == Symbol
    ConstantSEL.should == :'foo:with:with:'
  end

  it "of type 'char' or 'unsigned char' is available as a Fixnum in Ruby" do
    ConstantChar.class.should == Fixnum
    ConstantUnsignedChar.class.should == Fixnum
    ConstantChar.should == 42
    ConstantUnsignedChar.should == 42
  end

  it "of type 'short' or 'unsigned short' is available as a Fixnum in Ruby" do
    ConstantShort.class.should == Fixnum
    ConstantUnsignedShort.class.should == Fixnum
    ConstantShort.should == 42
    ConstantUnsignedShort.should == 42
  end

  it "of type 'int' or 'unsigned int' is available as a Fixnum in Ruby" do
    ConstantInt.class.should == Fixnum
    ConstantUnsignedInt.class.should == Fixnum
    ConstantInt.should == 42
    ConstantUnsignedInt.should == 42
  end

  it "of type 'long' or 'unsigned long' is available as a Fixnum in Ruby" do
    ConstantLong.class.should == Fixnum
    ConstantUnsignedLong.class.should == Fixnum
    ConstantLong.should == 42
    ConstantUnsignedLong.should == 42
  end

  it "of type 'long long' or 'unsigned long long' is available as a Fixnum in Ruby" do
    ConstantLongLong.class.should == Fixnum
    ConstantUnsignedLongLong.class.should == Fixnum
    ConstantLongLong.should == 42
    ConstantUnsignedLongLong.should == 42
  end

  it "of type 'float' or 'double' is available as a Float in Ruby" do
    ConstantFloat.class.should == Float
    ConstantDouble.class.should == Float
    ConstantFloat.should.be_close(3.1415, 0.0001)
    ConstantDouble.should.be_close(3.1415, 0.0001)
  end

  it "of type 'BOOL' is available as true/false in Ruby" do
    ConstantYES.class.should == TrueClass
    ConstantNO.class.should == FalseClass
    ConstantYES.should == true
    ConstantNO.should == false
  end

  it "of type 'NSPoint' is available as an NSPoint boxed instance in Ruby" do
    ConstantNSPoint.class.should == NSPoint
    ConstantNSPoint.should == NSPoint.new(1, 2)
  end

  it "of type 'NSSize' is available as an NSSize boxed instance in Ruby" do
    ConstantNSSize.class.should == NSSize
    ConstantNSSize.should == NSSize.new(3, 4)
  end

  it "of type 'NSRect' is available as an NSRect boxed instance in Ruby" do
    ConstantNSRect.class.should == NSRect
    ConstantNSRect.should == NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4))
  end
end
