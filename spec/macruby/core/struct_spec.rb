require File.dirname(__FILE__) + "/../spec_helper"

describe "A BridgeSupport structure" do
  it "is an instance of Boxed" do
    NSPoint.superclass.should == Boxed
    NSSize.superclass.should == Boxed
    NSRect.superclass.should == Boxed
    NSRange.superclass.should == Boxed
  end

  it "can be created with null values using the #new method with no argument" do
    o = NSPoint.new
    o.x.class.should == Float
    o.y.class.should == Float
    o.x.should == 0.0
    o.y.should == 0.0

    o = NSRect.new
    o.origin.class.should == NSPoint
    o.origin.x.class.should == Float
    o.origin.y.class.should == Float
    o.origin.x.should == 0.0
    o.origin.y.should == 0.0
    o.size.class.should == NSSize
    o.size.width.class.should == Float
    o.size.height.class.should == Float
    o.size.width.should == 0.0
    o.size.height.should == 0.0
  end

  it "can be created with given values using the #new method with arguments" do
    o = NSPoint.new(1.0, 2.0)
    o.y.class.should == Float
    o.x.class.should == Float
    o.x.should == 1.0
    o.y.should == 2.0

    o = NSPoint.new(1, 2)
    o.x.class.should == Float
    o.y.class.should == Float
    o.x.should == 1.0
    o.y.should == 2.0

    fix1 = Object.new; def fix1.to_f; 1.0; end
    fix2 = Object.new; def fix2.to_f; 2.0; end

    o = NSPoint.new(fix1, fix2)
    o.x.class.should == Float
    o.y.class.should == Float
    o.x.should == 1.0
    o.y.should == 2.0

    lambda { NSPoint.new(1) }.should raise_error(ArgumentError) 
    lambda { NSPoint.new(1, 2, 3) }.should raise_error(ArgumentError)
    lambda { NSPoint.new(Object.new, 42) }.should raise_error(TypeError)
    lambda { NSPoint.new(nil, nil) }.should raise_error(TypeError)

    o = NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4))
    o.origin.class.should == NSPoint
    o.origin.x.class.should == Float
    o.origin.y.class.should == Float
    o.origin.x == 1.0
    o.origin.y == 2.0
    o.size.class.should == NSSize
    o.size.width.class.should == Float
    o.size.height.class.should == Float
    o.size.width == 3.0
    o.size.height == 4.0

    lambda { NSRect.new(1) }.should raise_error(ArgumentError)
    lambda { NSRect.new(1, 2) }.should raise_error(TypeError)
    lambda { NSRect.new(1, 2, 3) }.should raise_error(ArgumentError)
    lambda { NSRect.new(1, 2, 3, 4) }.should raise_error(ArgumentError)
    lambda { NSRect.new(NSSize.new, NSPoint.new) }.should raise_error(TypeError)
    lambda { NSRect.new(nil, nil) }.should raise_error(TypeError)
  end

  it "has accessors for every field" do
    p = NSPoint.new
    p.x = 1
    p.y = 2
    p.x.class.should == Float
    p.y.class.should == Float
    p.x.should == 1.0
    p.y.should == 2.0

    s = NSSize.new
    s.width = 3
    s.height = 4
    s.width.class.should == Float
    s.height.class.should == Float
    s.width.should == 3.0
    s.height.should == 4.0

    r = NSRect.new
    r.origin = p
    r.size = s
    r.origin.class.should == NSPoint
    r.size.class.should == NSSize

    lambda { r.origin = nil }.should raise_error(TypeError)
    lambda { r.origin = Object.new }.should raise_error(TypeError)

    r.origin = [123, 456]
    r.size = [789, 100]
    
    r.origin.x.class.should == Float
    r.origin.y.class.should == Float
    r.size.width.class.should == Float
    r.size.height.class.should == Float
    r.origin.x.should == 123.0
    r.origin.y.should == 456.0
    r.size.width.should == 789.0
    r.size.height.should == 100.0
  end

  it "can be compared to another similar object using #==" do
    p1 = NSPoint.new(1, 2)
    p2 = NSPoint.new(3, 4)
    p1.should_not == p2

    p2.x = 1
    p2.y = 2
    p1.should == p2

    r1 = NSRect.new
    r2 = NSRect.new
    r1.should == r2

    r1.origin = NSPoint.new(1, 2)
    r1.size = NSSize.new(3, 4)
    r2.origin = NSPoint.new(1, 2)
    r2.size = NSSize.new(3, 42)
    r1.should_not == r2

    r2.size.height = 4
    r1.should == r2

    NSPoint.new.should_not == nil
    NSPoint.new.should_not == 123
    NSPoint.new.should_not == [0, 0]
    NSPoint.new.should_not == [0.0, 0.0]
    NSPoint.new.should_not == NSSize.new
  end

  it "has a nice #inspect message that lists the fields" do
    p = NSPoint.new
    p.inspect.should == "#<CGPoint x=0.0 y=0.0>"
    p.x = 1
    p.y = 2
    p.inspect.should == "#<CGPoint x=1.0 y=2.0>"

    s = NSSize.new(3, 4)
    s.inspect.should == "#<CGSize width=3.0 height=4.0>"

    r = NSRect.new
    r.inspect.should == "#<CGRect origin=#<CGPoint x=0.0 y=0.0> size=#<CGSize width=0.0 height=0.0>>"
    r.origin = p
    r.inspect.should == "#<CGRect origin=#<CGPoint x=1.0 y=2.0> size=#<CGSize width=0.0 height=0.0>>"
    r.size = s
    r.inspect.should == "#<CGRect origin=#<CGPoint x=1.0 y=2.0> size=#<CGSize width=3.0 height=4.0>>"
  end

  it "can be duplicated using #dup or #clone" do
    p = NSPoint.new(1, 2)
    p.should == p.dup
    p.should == p.clone

    p2 = p.dup
    p2.x = 42
    p2.should_not == p

    r = NSRect.new
    r.origin.x = 1
    r.origin.y = 2
    r2 = r.dup
    r.size.width = 3.0
    r.size.height = 4.0
    r2.size = NSSize.new(3, 4)
    r.should == r2
  end

  it "returns the list of fields when the #fields class method is called" do
    NSPoint.fields.should == [:x, :y]
    NSSize.fields.should == [:width, :height]
    NSRect.fields.should == [:origin, :size]
  end

  it "returns false when the #opaque? class method is called" do
    NSPoint.opaque?.should == false
    NSSize.opaque?.should == false
    NSRect.opaque?.should == false
  end

  it "returns its Objective-C encoding type when then #type class method is called" do
    if RUBY_ARCH == 'x86_64'
      NSPoint.type.should == '{CGPoint=dd}'
      NSSize.type.should == '{CGSize=dd}'
      NSRect.type.should == '{CGRect={CGPoint=dd}{CGSize=dd}}'
    else
      NSPoint.type.should == '{_NSPoint=ff}'
      NSSize.type.should == '{_NSSize=ff}'
      NSRect.type.should == '{_NSRect={_NSPoint=ff}{_NSSize=ff}}'
    end
  end

  it "defined after a structure which has the same type is an alias to the other structure class" do
    NSPoint.should == CGPoint
    NSSize.should == CGSize
    NSRect.should == CGRect
    NSPoint.object_id.should == CGPoint.object_id
    NSSize.object_id.should == CGSize.object_id
    NSRect.object_id.should == CGRect.object_id
  end

  it "returns an Array based on its elements when #to_a is called" do
    p = NSPoint.new(1, 2)
    o = p.to_a
    o.class.should == Array
    o.should == [1, 2]

    r = NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4))
    o = r.to_a
    o.class.should == Array
    o.should == [NSPoint.new(1, 2), NSSize.new(3, 4)]
    o.map { |x| x.to_a }.flatten.should == [1, 2, 3, 4]
  end
end
