require File.dirname(__FILE__) + '/../spec_helper'

framework 'Foundation'

describe "A Pointer object" do
=begin # TODO
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
    lambda { Pointer.new('invalid') }.should raise_error(TypeError)
  end
=end

=begin # TODO
  it "can be assigned an object of the given type and retrieve it later, using #[] and #[]=" 
    p = Pointer.new('@')
    o = Object.new
    p[0] = o
    p[0].should == o

    p = Pointer.new('i')
    p[0] = 42
    p[0].should == 42

    lambda { p[0] = Object.new }.should raise_error(TypeError)
  end
=end 
end
