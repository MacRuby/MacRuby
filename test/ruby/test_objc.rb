require 'test/unit'

class TestObjC < Test::Unit::TestCase

  def setup
    framework 'Foundation'
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
    # FIXME this doesn't work yet
    #obj = TestSuperMethod.new
    #assert_equal(42, obj.foo)
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
    # TODO this currently SEGV
    #m = String
    #assert_equal(m, m.self)
  end

  def test_call_superclass
    o = Object.new
    assert_equal(NSObject, o.superclass)
    s = 'foo'
    assert_equal(NSMutableString, s.superclass)
    n = 42
    assert_equal(NSNumber, n.superclass)
    n = nil 
    assert_equal(NSObject, n.superclass)
  end

  def test_no_direct_nsobject_subclass
    old_verbose = $VERBOSE
    $VERBOSE = nil # no warn
    klass = eval("class Foo < NSObject; end; Foo")
    assert_equal(Object, klass.superclass)
    $VERBOSE = old_verbose
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

end

