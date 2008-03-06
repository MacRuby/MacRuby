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
    s = <<EOS
#import <Foundation/Foundation.h>
@interface MyClass : NSObject
@end
@implementation MyClass
- (int)test
{
    return 42;
}
@end
EOS
    File.open('/tmp/_test.m', 'w') { |io| io.write(s) }
    system("gcc /tmp/_test.m -bundle -o /tmp/_test.bundle -framework Foundation -fobjc-gc-only") or exit 1
    require 'dl'; DL.dlopen('/tmp/_test.bundle')
    File.unlink('/tmp/_test.m')
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

  def test_method_dispatch_priority
    o = MyClass.new
    assert_kind_of(MyClass, o)
    assert_equal(42, o.test)
  end

end
