require 'test/unit'

class TestObjC < Test::Unit::TestCase

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

end

