require File.dirname(__FILE__) + '/../spec_helper'

framework 'Foundation'

describe "A Pointer object" do
  it "can be created using the #new class method and with a valid Objective-C type or a valid Symbol object" do
    types = {
      :object     => '@',
      :char       => 'c',
      :uchar      => 'C',
      :short      => 's',
      :ushort     => 'S',
      :int        => 'i',
      :uint       => 'I',
      :long       => 'l',
      :ulong      => 'L',
      :long_long  => 'q',
      :ulong_long => 'Q',
      :float      => 'f',
      :double     => 'd'
    }

    types.each do |sym, typestr|
      p = nil
      lambda { p = Pointer.new(sym) }.should_not raise_error
      p.type.should == typestr

      lambda { p = Pointer.new(typestr) }.should_not raise_error
      p.type.should == typestr
    end

    lambda { Pointer.new }.should raise_error(ArgumentError)
    lambda { Pointer.new(nil) }.should raise_error(TypeError)
    lambda { Pointer.new(123) }.should raise_error(TypeError)
    lambda { Pointer.new('x') }.should raise_error(TypeError)

    p = Pointer.new(NSRect.type)
    p.type.should == NSRect.type
  end

  it "can be assigned an object of the given type and retrieve it later, using #[] and #[]=" do
    p = Pointer.new('object')
    o = Object.new
    p[0] = o
    p[0].object_id.should == o.object_id
    p[0].should == o

    p[0] = 123
    p[0].class.should == Fixnum
    p[0].should == 123

    int_types = %w{ char uchar short ushort int uint long ulong
                    long_long ulong_long }
    int_types.each do |t|
      p = Pointer.new(t)
      p[0] = 42
      p[0].class.should == Fixnum
      p[0].should == 42

      o = Object.new
      def o.to_i; 42; end

      p[0] = o
      p[0].class.should == Fixnum
      p[0].should == 42

      lambda { p[0] = Object.new }.should raise_error(TypeError)
    end

    float_types = %w{ float double }
    float_types.each do |t|
      p = Pointer.new(t)
      p[0] = 42
      p[0].class.should == Float
      p[0].should == 42.0

      o = Object.new
      def o.to_f; 42.0; end

      p[0] = o
      p[0].class.should == Float
      p[0].should == 42.0

      lambda { p[0] = Object.new }.should raise_error(TypeError)
    end

    struct_types = [[NSPoint, NSPoint.new(1, 2)],
                    [NSSize, NSSize.new(3, 4)],
                    [NSRect, NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4))]]
    struct_types.each do |k, o|
      p = Pointer.new(k.type)
      p[0] = o
      p[0].class.should == k
      p[0].should == o

      lambda { p[0] = Object.new }.should raise_error(TypeError)
    end
  end
end
