require 'test/unit'

DUMMY_M = <<END_DUMMY_M
#import <Foundation/Foundation.h>

@interface TestGetMethod : NSObject
@end

@implementation TestGetMethod
- (void)getInt:(int *)v {}
- (void)getLong:(long *)l {}
- (void)getObject:(id *)o {}
- (void)getRect:(NSRect *)r {}
@end

@interface Dummy : NSObject
{
    int x;
}
@end

@implementation Dummy

- (void)testCallGetIntMethod:(id)receiver expectedValue:(int)val
{
    int i = 0;
    [(TestGetMethod *)receiver getInt:&i];
    if (i != val)
        [NSException raise:@"testCallGetIntMethod" format:@"expected %d, got %d", val, i];
}

- (void)testCallGetLongMethod:(id)receiver expectedValue:(long)val
{
    long l = 0;
    [(TestGetMethod *)receiver getLong:&l];
    if (l != val)
        [NSException raise:@"testCallGetLongMethod" format:@"expected %ld, got %ld", val, l];
}

- (void)testCallGetObjectMethod:(id)receiver expectedValue:(id)val
{
    id o = nil;
    [(TestGetMethod *)receiver getObject:&o];
    if (o != val)
        [NSException raise:@"testCallGetLongMethod" format:@"expected %p, got %p", val, o];
}

- (void)testCallGetRectMethod:(id)receiver expectedValue:(NSRect)val
{
    NSRect r = NSZeroRect;
    [(TestGetMethod *)receiver getRect:&r];
    if (!NSEqualRects(r, val)) 
        [NSException raise:@"testCallGetLongMethod" format:@"expected %@, got %@", NSStringFromRect(val), NSStringFromRect(r)];
}

- (int)X
{
    return x;
}

- (void)setX:(int)aX
{
    x = aX;
}

- (int)isX
{
    return x == 1;
}

@end

void Init_dummy(void) {}
END_DUMMY_M

if !File.exist?('/tmp/dummy.bundle') or File.mtime(__FILE__) > File.mtime('/tmp/dummy.bundle')
  File.open('/tmp/dummy.m', 'w') { |io| io.write(DUMMY_M) }
  system("/usr/bin/gcc /tmp/dummy.m -o /tmp/dummy.bundle -g -framework Foundation -dynamiclib -fobjc-gc -arch i386 -arch x86_64 -arch ppc")
end
require '/tmp/dummy.bundle'

class TestObjC < Test::Unit::TestCase

  def setup
    framework 'Foundation'
    framework 'AppKit'
  end

  def test_all_objects_inherit_from_nsobject
    assert_kind_of(NSObject, true)
    assert_kind_of(NSObject, false)
    assert_kind_of(NSObject, 42)
    assert_kind_of(NSObject, 42.42)
    assert_kind_of(NSObject, 42_000_000_000_000)
    assert_kind_of(NSObject, 'foo')
    assert_kind_of(NSObject, [])
  end

  def test_class_shortcuts
    assert_equal(NSObject, Object)
    assert_equal(NSMutableString, String)
    assert_equal(NSMutableArray, Array)
    assert_equal(NSMutableDictionary, Hash)
  end

  def test_instance_of_on_primitive_types
    o = 'foo'
    assert(o.instance_of?(NSMutableString))
    assert(o.instance_of?(String))
    assert(o.kind_of?(String))
    assert_equal(String, o.class)
    o = [42]
    assert(o.instance_of?(NSMutableArray))
    assert(o.instance_of?(Array))
    assert(o.kind_of?(Array))
    assert_equal(Array, o.class)
    o = {42=>42}
    assert(o.instance_of?(NSMutableDictionary))
    assert(o.instance_of?(Hash))
    assert(o.kind_of?(Hash))
    assert_equal(Hash, o.class)
  end

  class ClassWithNamedArg
    def doSomethingWith(x, andObject:y, andObject:z)
      x + y + z
    end
    def doSomethingWith(x, andObject:y)
      x + y
    end
  end

  def test_named_argument
    o = ClassWithNamedArg.new
    a = o.doSomethingWith('x', andObject:'y', andObject:'z')
    assert_equal('xyz', a)

    a = o.performSelector('doSomethingWith:andObject:',
			  withObject:'x', withObject:'y')
    assert_equal('xy', a)
  end

  def test_named_argument_metaclass
    o = Object.new
    def o.doSomethingWith(x, andObject:y, andObject:z)
      (x + y + z) * 2
    end
    def o.doSomethingWith(x, andObject:y)
      (x + y) * 2
    end

    a = o.doSomethingWith('x', andObject:'y', andObject:'z')
    assert_equal('xyzxyz', a)

    a = o.performSelector('doSomethingWith:andObject:',
			  withObject:'x', withObject:'y')
    assert_equal('xyxy', a)
  end

  module DispatchModule
    def foo(x)
      x
    end
    def foo(x, with:y)
      x + y
    end
  end
  class DispatchClass 
    include DispatchModule
  end
  def test_objc_dispatch_on_module_function
    o = DispatchClass.new
    r = o.performSelector('foo:', withObject:'xxx')
    assert_equal('xxx', r)
    r = o.performSelector('foo:with:', withObject:'xxx', withObject:'yyy')
    assert_equal('xxxyyy', r)
  end

  class TestSuperMethod
    def init
      super
      @foo = 42
      self
    end
    attr_reader :foo
  end
  def test_super_method
    obj = TestSuperMethod.alloc.init
    assert_equal(42, obj.foo)
    obj = TestSuperMethod.performSelector(:alloc).performSelector(:init)
    assert_equal(42, obj.foo)
    obj = TestSuperMethod.new
    assert_equal(42, obj.foo)
    obj = TestSuperMethod.performSelector(:new)
    assert_equal(42, obj.foo)
  end

  def test_pure_objc_ivar
    o = NSObject.alloc.init
    assert_kind_of(NSObject, o)
    o.instance_variable_set(:@foo, 'foo')
    GC.start
    assert_equal('foo', o.instance_variable_get(:@foo))
  end

  def test_method_variadic
    p = NSPredicate.predicateWithFormat('foo == 1')
    assert_kind_of(NSPredicate, p)
    assert_equal('foo == 1', p.predicateFormat)
    p = NSPredicate.predicateWithFormat('foo == %@', 'bar')
    assert_kind_of(NSPredicate, p)
    assert_equal('foo == "bar"', p.predicateFormat)
    p = NSPredicate.predicateWithFormat('%@ == %@', 'foo', 'bar')
    assert_kind_of(NSPredicate, p)
    assert_equal('"foo" == "bar"', p.predicateFormat)
  end

  def test_struct_create
    p = NSPoint.new
    assert_kind_of(NSPoint, p)
    assert_kind_of(Float, p.x)
    assert_equal(0.0, p.x)
    assert_kind_of(Float, p.y)
    assert_equal(0.0, p.y)

    p = NSPoint.new(1, 2)
    assert_kind_of(Float, p.x)
    assert_equal(1.0, p.x)
    assert_kind_of(Float, p.y)
    assert_equal(2.0, p.y)

    assert_raise(ArgumentError) { NSPoint.new(1) }
    assert_raise(ArgumentError) { NSPoint.new(1, 2, 3) }
    assert_raise(ArgumentError) { NSPoint.new('x', 'y') }

    r = NSRect.new
    assert_equal(NSPoint.new(0, 0), r.origin)
    assert_equal(NSSize.new(0, 0), r.size)

    r = NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4))
    assert_equal(NSPoint.new(1, 2), r.origin)
    assert_equal(NSSize.new(3, 4), r.size)

    r.origin.x = 42
    r.size.width = 42

    assert_equal(NSRect.new(NSPoint.new(42, 2), NSSize.new(42, 4)), r)
  end

  def test_create_struct_with_array
    s = NSStringFromSize([1, 2])
    assert_equal(NSSize.new(1, 2), NSSizeFromString(s))
    
    assert_raise(ArgumentError) { NSStringFromSize([]) }
    assert_raise(ArgumentError) { NSStringFromSize([1]) }
    assert_raise(ArgumentError) { NSStringFromSize([1, 2, 3]) }

    s = NSStringFromRect([[1, 2], [3, 4]])
    assert_equal(NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4)), 
		 NSRectFromString(s))

    a = [1, 2, 3, 4]
    s = NSStringFromRect(a)
    assert_equal(NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4)), 
		 NSRectFromString(s))
    assert_equal([1, 2, 3, 4], a)

    assert_raise(ArgumentError) { NSStringFromRect([]) }
    assert_raise(ArgumentError) { NSStringFromRect([1]) }
    assert_raise(ArgumentError) { NSStringFromRect([1, 2]) }
    assert_raise(ArgumentError) { NSStringFromRect([1, 2, 3, 4, 5]) }
  end

  def test_struct_inspect
    r = NSRect.new
    if RUBY_ARCH == 'x86_64'
      assert_equal("#<NSRect origin=#<CGPoint x=0.0 y=0.0> size=#<CGSize width=0.0 height=0.0>>",
                   r.inspect)
    else
      assert_equal("#<NSRect origin=#<NSPoint x=0.0 y=0.0> size=#<NSSize width=0.0 height=0.0>>",
                   r.inspect)
    end
    r.origin.x = 42
    r.size.width = 42
    if RUBY_ARCH == 'x86_64'
      assert_equal("#<NSRect origin=#<CGPoint x=42.0 y=0.0> size=#<CGSize width=42.0 height=0.0>>",
                   r.inspect)
    else
      assert_equal("#<NSRect origin=#<NSPoint x=42.0 y=0.0> size=#<NSSize width=42.0 height=0.0>>",
                   r.inspect)
    end
  end

  def test_struct_dup
    r = NSMakeRect(1, 2, 3, 4)
    r2 = r.dup
    if RUBY_ARCH == 'x86_64'
      assert_kind_of(CGRect, r2)
    else
      assert_kind_of(NSRect, r2)
    end
    assert_equal(r, r2)
    r2.origin.x = 42
    assert(r != r2) 
    r2.origin.x = 1
    assert_equal(r, r2)
  end

  class TestInitCallInitialize
    attr_reader :foo
    def initialize
      @foo = 42
    end
  end
  def test_init_call_initialize
    o = TestInitCallInitialize.new
    assert_equal(42, o.foo)
    o = TestInitCallInitialize.alloc.init
    assert_equal(42, o.foo)
  end

  def test_call_self
    o = Object.new
    assert_equal(o, o.self)
    s = 'foo'
    assert_equal(s, s.self)
    n = 42
    assert_kind_of(NSNumber, n.self)
    assert_equal(42, n.self.intValue)
    n = nil
    assert_kind_of(NSNull, n.self)
    assert_equal(NSNull.null, n.self)
    m = String
    assert_equal(m, m.self)
  end

  def test_call_superclass
    o = Object.new
    assert_equal(nil, o.superclass)
    s = 'foo'
    assert_equal(NSMutableString, s.superclass)
    n = nil 
    assert_equal(NSObject, n.superclass)
  end

  class TestSuper1
    def foo; 'xxx'; end
  end
  class TestSuper2 < TestSuper1
    def bar; __super_objc_send__(:foo); end
  end
  def test_objc_super
    o = TestSuper2.new
    assert_equal('xxx', o.bar)
  end

  class RubyTestGetMethod < TestGetMethod
    attr_accessor :tc, :val
    [:getInt, :getLong, :getObject, :getRect].each do |s|
      define_method(s) do |ptr|
        @tc.assert_kind_of(Pointer, ptr)
        ptr.assign(@val)
      end
    end
  end
  def test_call_get_int
    d = Dummy.new
    obj = RubyTestGetMethod.new
    obj.tc = self
    obj.val = 42
    d.testCallGetIntMethod(obj, expectedValue:obj.val)
    obj.val = 42_000_000
    d.testCallGetLongMethod(obj, expectedValue:obj.val)
    obj.val = d
    d.testCallGetObjectMethod(obj, expectedValue:obj.val)
    obj.val = NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4))
    d.testCallGetRectMethod(obj, expectedValue:obj.val)
  end

  class Icon
    attr_accessor :name
    def initialize(name)
      @name = name
    end
  end
  def test_NSKVONotifying_class_preserve_ivars
    array_controller = NSArrayController.new
    array_controller.setAvoidsEmptySelection(false)
    array_controller.setPreservesSelection(false)
    array_controller.setSelectsInsertedObjects(false)
    array_controller.setAutomaticallyRearrangesObjects(true)
    array_controller.setSortDescriptors([NSSortDescriptor.alloc.initWithKey("name", ascending: false)])
    array_controller.addObjects([Icon.new("Rich"), Icon.new("Chad")])
    o = array_controller.arrangedObjects[0]
    assert_equal(Icon, o.class)
    assert_equal('Rich', o.name)
    o = array_controller.arrangedObjects[1]
    assert_equal(Icon, o.class)
    assert_equal('Chad', o.name)
  end

  class TestKVCSetter
    attr_accessor :foo
  end
  def test_KVO_setter_defined_by_attr_setter
    o = TestKVCSetter.new
    assert(o.respond_to?(:foo=))
    assert(o.respond_to?(:setFoo))
    o.setFoo(42)
    assert_equal(42, o.foo)
    assert_equal(nil, o.performSelector('setFoo:', withObject:42))
    assert_equal(42, o.foo)
    assert_equal(nil, o.send('setFoo', 42))
    assert_equal(42, o.foo)
    assert_equal(nil, o.send('setFoo:', 42))
    assert_equal(42, o.foo)
  end

  def test_respond_to_objc_methods
    s = ''
    assert(s.respond_to?(:strip))
    assert(s.respond_to?(:setString))
    assert(s.respond_to?('setString:'))
    assert(s.respond_to?('performSelector:withObject:'))
  end

  class TestCallSuperOverridenNew
    def self.new(x)
      super
    end
    def initialize(x)
      @x = x
    end
    attr_reader :x
  end
  def test_call_super_overriden_new
    o = TestCallSuperOverridenNew.new(42)
    assert_equal(42, o.x)
  end

  def test_class_on_mods
    assert_equal(Class, Object.class)
    assert_equal(Module, Kernel.class)
    assert_equal(Class, Class.new.class)
    assert_equal(Module, Module.new.class)
  end 

  def test_protocol
    o = Object.new
    # TODO object should automatically conform to protocol when all mandatory methods are implemented.
    #assert(!o.conformsToProtocol(Protocol.protocolWithName('NSMutableCopying')))
    def o.mutableCopyWithZone(zone); self; end
    #assert(o.conformsToProtocol(Protocol.protocolWithName('NSMutableCopying')))
    assert_equal(o, o.mutableCopy)
    assert_equal('^{_NSZone=}', o.methodSignatureForSelector('mutableCopyWithZone:').getArgumentTypeAtIndex(2))
  end 

  def test_create_pointer
    assert_equal(nil, NSString.stringWithContentsOfFile('/does/not/exist', encoding:NSASCIIStringEncoding, error:nil))
    p = Pointer.new_with_type('@')
    p.assign(nil)
    assert_equal(nil, NSString.stringWithContentsOfFile('/does/not/exist', encoding:NSASCIIStringEncoding, error:p))
    err = p[0]
    assert_kind_of(NSError, err) 
    p.assign(nil)
    assert(NSString.stringWithContentsOfFile(__FILE__, encoding:NSASCIIStringEncoding, error:p))
    assert_equal(nil, p[0])
  end

  def test_create_pointer2
    p1 = Pointer.new_with_type(NSRect.type)
    p2 = Pointer.new_with_type(NSRect.type)
    NSDivideRect([0, 0, 100, 100], p1, p2, 10.0, 0)
    assert_equal(NSMakeRect(0, 0, 10, 100), p1[0])
    assert_equal(NSMakeRect(10, 0, 90, 100), p2[0])
  end

  def test_conversion_helpers
    o = Dummy.new
    o.setX(1)
    assert_equal(1, o.X)
    assert_equal(1, o.isX)
    o.X = 2
    assert_equal(2, o.X)
    assert_equal(0, o.X?)
    o = NSBundle.mainBundle
    assert_equal(true, o.isLoaded)
    assert_equal(true, o.loaded?)
  end

  class TestChangeSignature
    def foo; 42; end
    def foo(x, with:y); 42; end
    method_signature 'foo', 'v@:'
    method_signature 'foo:with:', 'v@:ii'
  end

  def test_change_method_signature
    o = TestChangeSignature.new 
    ms = o.methodSignatureForSelector('foo')
    assert_equal('v', ms.methodReturnType)
    assert_equal('@', ms.getArgumentTypeAtIndex(0))
    assert_equal(':', ms.getArgumentTypeAtIndex(1))
    ms = o.methodSignatureForSelector('foo:with:')
    assert_equal('v', ms.methodReturnType)
    assert_equal('@', ms.getArgumentTypeAtIndex(0))
    assert_equal(':', ms.getArgumentTypeAtIndex(1))
    assert_equal('i', ms.getArgumentTypeAtIndex(2))
    assert_equal('i', ms.getArgumentTypeAtIndex(3))
  end
end
