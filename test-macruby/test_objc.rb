require 'test/unit'

class MyDictionary < NSMutableDictionary
  def foo(tc, *args)
    tc.assert_kind_of(Array, args)
    unless args.empty?
      tc.assert_equal(1, args.length)
      tc.assert_kind_of(Hash, args[0])
    end
  end
  def foo2(tc, args)
    tc.assert_kind_of(Hash, args)
  end
end

class TestSubclass < Test::Unit::TestCase

  def setup
    framework 'Foundation'
  end

  def test_new
    o = NSObject.new
    assert_kind_of(NSObject, o)
    o = NSObject.alloc.init
    assert_kind_of(NSObject, o)
  end

  def test_keyed_syntax
    d = NSMutableDictionary.new
    o = NSObject.new
    k = NSString.new
    d.setObject o, forKey:k
    assert_equal(o, d.objectForKey(k))

    d2 = MyDictionary.new
    d2.foo(self)
    d2.foo(self, foo:k)
    d2.foo(self, foo:k, bar:k)
    d2.foo2(self, foo:k, bar:k)

    d2 = NSMutableDictionary.performSelector('new')
    d2.performSelector(:'setObject:forKey:', withObject:o, withObject:k)
    assert_equal(o, d2.performSelector(:'objectForKey:', withObject:k))
    assert(d.isEqual(d2))
  end

  def test_pure_objc_ivar
    o = NSObject.alloc.init
    assert_kind_of(NSObject, o)
    o.instance_variable_set(:@foo, 'foo')
    GC.start
    assert_equal('foo', o.instance_variable_get(:@foo))
  end

  def test_cftype_ivar
    o = NSString.alloc.init
    assert_kind_of(NSString, o)
    o.instance_variable_set(:@foo, 'foo')
    GC.start
    assert_equal('foo', o.instance_variable_get(:@foo))
  end

end
