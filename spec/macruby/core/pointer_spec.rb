require File.dirname(__FILE__) + "/../spec_helper"

describe "A Pointer object, when initializing" do
  before :all do
    @types = {
      :object     => '@',
      :id         => '@',
      :class      => '#',
      :boolean    => 'B',
      :bool       => 'B',
      :selector   => ':',
      :sel        => ':',
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
  end

  it "accepts a valid Objective-C type string" do
    @types.values.each do |type|
      lambda { @pointer = Pointer.new(type) }.should_not raise_error
      @pointer.type.should == type
    end
  end

  it "accepts a Symbol argument as a type shortcut" do
    @types.each do |symbol, type|
      lambda { @pointer = Pointer.new(symbol) }.should_not raise_error
      @pointer.type.should == type
    end
  end

  it "raises an ArgumentError when no argument is given" do
    lambda { Pointer.new }.should raise_error(ArgumentError)
  end

  it "raises an ArgumentError when a given Symbol is an invalid type shortcut" do    lambda { Pointer.new(:foo) }.should raise_error(TypeError)
  end

  it "raises a TypeError when a incompatible object is given" do
    lambda { Pointer.new(nil) }.should raise_error(TypeError)
    lambda { Pointer.new(123) }.should raise_error(TypeError)
    lambda { Pointer.new('x') }.should raise_error(TypeError)
  end

  it "accepts the type returned by NSRect" do
    Pointer.new(NSRect.type).type.should == NSRect.type
  end

  it "accepts a non mandatory second argument that specifies the number of elements that should be allocated" do
    lambda { Pointer.new('i', 1) }.should_not raise_error
  end

  it "raises an ArgumentError if the second argument (size) is not greater than 0" do
    lambda { Pointer.new('i', -1) }.should raise_error(ArgumentError)
    lambda { Pointer.new('i', 0) }.should raise_error(ArgumentError)
  end
end

describe "Pointer, through #[] and #[]=" do
  integer_types = %w{ char uchar short ushort int uint long ulong long_long ulong_long }.map { |x| x.intern }
  float_types   = %w{ float double }.map { |x| x.intern }

  structs       = [NSPoint.new(1, 2), NSSize.new(3, 4), NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4))]
  struct_types  = structs.map { |x| x.class.type }
  structs       = structs.zip(struct_types)

  it "can assign and retrieve any object with type `object'" do
    pointer = Pointer.new(:object)
    [Object.new, 123].each do |object|
      pointer[0] = object
      pointer[0].should == object
      pointer[0].should be_kind_of(object.class)
    end
  end

  integer_types.each do |type|
    it "can assign and retrieve Fixnum compatible objects for type `#{type}'" do
      pointer = Pointer.new(type)

      coercable_object = Object.new
      def coercable_object.to_i; 42; end

      [42, coercable_object].each do |object|
        pointer[0] = object
        pointer[0].should == 42
        pointer[0].should be_kind_of(Fixnum)
      end
    end
  end

  float_types.each do |type|
    it "can assign and retrieve Float compatible objects for type `#{type}'" do
      pointer = Pointer.new(type)

      coercable_object = Object.new
      def coercable_object.to_f; 42.0; end

      [42, coercable_object].each do |object|
        pointer[0] = object
        pointer[0].should == 42.0
        pointer[0].should be_kind_of(Float)
      end
    end
  end

  structs.each do |struct, type|
    it "can assign and retrieve #{struct.class.name} objects for type `#{type}'" do
      pointer = Pointer.new(type)

      pointer[0] = struct
      pointer[0].should == struct
      pointer[0].should be_kind_of(struct.class)
    end
  end

  (integer_types + float_types + struct_types).each do |type|
    it "raises a TypeError when assigned an object not of type `#{type}'" do
      pointer = Pointer.new(type)
      lambda { pointer[0] = Object.new }.should raise_error(TypeError)
    end
  end

  it "can assign and retrieve CF type objects" do
    ptr = Pointer.new('^{__CFError}')
    ptr[0].should == nil
    CFURLResourceIsReachable(NSURL.URLWithString('http://doesnotexistomgwtf.be'), ptr).should == false
    ptr[0].is_a?(NSError).should == true
  end

  it "handle 'void *' C pointers as 'unsigned char *'" do
    ptr = 'hey'.dataUsingEncoding(NSMacOSRomanStringEncoding).bytes
    ptr.class.should == Pointer
    ptr.type.should == 'C'
    ptr[0].class.should == Fixnum
    ptr[0].chr.should == 'h'
    ptr[1].class.should == Fixnum
    ptr[1].chr.should == 'e'
    ptr[2].class.should == Fixnum
    ptr[2].chr.should == 'y'
  end

  it "will raise a TypeError exception in case the given index cannot be converted to a numeric type" do
    pointer = Pointer.new('i')
    lambda { pointer[nil] }.should raise_error(TypeError)
    lambda { pointer[nil] = 42 }.should raise_error(TypeError)
    lambda { pointer['omg'] }.should raise_error(TypeError)
    lambda { pointer['omg'] = 42 }.should raise_error(TypeError)
  end

  it "will raise an ArgumentError exception in case the given index is negative" do
    pointer = Pointer.new('i')
    lambda { pointer[-1] }.should raise_error(ArgumentError)
    lambda { pointer[-1] = 42 }.should raise_error(ArgumentError)
  end

  it "will raise an ArgumentError exception in case the given index is out of bounds" do
    pointer = Pointer.new('i')
    lambda { pointer[0] }.should_not raise_error
    lambda { pointer[0] = 42 }.should_not raise_error
    lambda { pointer[1] }.should raise_error(ArgumentError)
    lambda { pointer[1] = 42 }.should raise_error(ArgumentError)

    pointer = Pointer.new('i', 3)
    3.times do |i|
      lambda { pointer[i] }.should_not raise_error
      lambda { pointer[i] = 42 }.should_not raise_error
    end
    lambda { pointer[3] }.should raise_error(ArgumentError)
    lambda { pointer[3] = 42 }.should raise_error(ArgumentError)
  end
end

describe "A Pointer object" do
  it "can have its type changed using #cast!" do
    pointer = Pointer.new(NSRect.type)
    pointer[0] = NSMakeRect(10, 20, 30, 40)

    val = NSValue.valueWithPointer(pointer)
    pointer2 = val.pointerValue
    pointer2.class.should == Pointer
    pointer2.type == 'v'

    pointer2.cast!(NSRect.type).should == pointer2
    pointer2.type.should == NSRect.type
    pointer2[0].should == NSMakeRect(10, 20, 30, 40)
  end

  it "raises a TypeError when a incompatible object is given into #cast!" do
    lambda {
      pointer = Pointer.new('i')
      pointer.cast!('x')
    }.should raise_error(TypeError)
  end

  it "of type 'c' can be passed as a C-style char array argument" do
    s = NSString.stringWithString('foo')
    ptr = Pointer.new(:char, 4)
    s.getCString(ptr, maxLength: 4, encoding: NSASCIIStringEncoding).should == true
    ptr[0].should == 102
    ptr[1].should == 111
    ptr[2].should == 111
    ptr[3].should == 0
  end

  it "respond to #+ and #- which will respectively return a new Pointer object based on the appropriate offset" do
    ptr = Pointer.new(:long, 10)
    10.times { |i| ptr[i] = i }
    ptr2 = ptr + 5;
    ptr2.class.should == Pointer
    ptr2[0].should == 5
    ptr2[1].should == 6
    ptr2[2].should == 7
    ptr2[3].should == 8
    ptr2[4].should == 9
    lambda { ptr2[5] }.should raise_error(ArgumentError)
    lambda { ptr2 + 6 }.should raise_error(ArgumentError)
    ptr3 = ptr2 - 5
    ptr3.class.should == Pointer
    10.times { |i| ptr3[i].should == i }
  end

  it "responds to #to_object which returns a copy of self casted to an objective-c object" do
    keyboard = TISCopyCurrentKeyboardInputSource()
    name = TISGetInputSourceProperty(keyboard, KTISPropertyLocalizedName)
    name.to_object.should be_an_instance_of String
    name.should be_an_instance_of Pointer
    # invalid use of #to_object will crash MacRuby, so we can't really test that
  end
end

describe "A pointer magic cookie" do
  before :all do
    @ptr = Pointer.magic_cookie(42);
  end

  it "has a type of ^v" do
    @ptr.type.should == '^v'
  end

  it "can be passed as a void* argument" do
    val = NSValue.valueWithPointer(@ptr)
    ptr = Pointer.new(:long)
    val.getValue(ptr)
    ptr[0].should == 42
  end

  it "cannot be accessed" do
    lambda { @ptr[0] }.should raise_error(ArgumentError)
    lambda { @ptr[0]=42 }.should raise_error(ArgumentError)
    lambda { @ptr.cast!(:float) }.should raise_error(ArgumentError)
    lambda { @ptr - 1 }.should raise_error(ArgumentError)
    lambda { @ptr + 1 }.should raise_error(ArgumentError)
  end
end
