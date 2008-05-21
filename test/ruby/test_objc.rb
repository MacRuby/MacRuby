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
    def doSomethingWith(ary, andObject:obj1, andObject:obj2)
      ary << obj1
      ary << obj2
      return ary
    end
    def doSomethingWith(ary, andObject:obj)
      ary << obj
      return ary
    end
  end

  def test_named_argument_call
    o = ClassWithNamedArg.new
    a = []
    a2 = o.doSomethingWith(a, andObject:'x', andObject:'y')
    assert_equal(a, a2)
    assert_equal(['x', 'y'], a)

    a = []
    a2 = o.performSelector('doSomethingWith:andObject:',
			   withObject:a, withObject:'xxx')
    assert_equal(a, a2)
    assert_equal(['xxx'], a)

    o = Object.new
    def o.doSomethingWith(ary, andObject:obj)
      ary << obj
      return ary
    end

    a = []
    a2 = o.doSomethingWith(a, andObject:'xxx')
    assert_equal(a, a2)
    assert_equal(['xxx'], a)

    a = []
    a2 = o.performSelector('doSomethingWith:andObject:',
			   withObject:a, withObject:'xxx')
    assert_equal(a, a2)
    assert_equal(['xxx'], a)
  end

end

