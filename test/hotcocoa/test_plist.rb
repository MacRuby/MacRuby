require 'test/unit'
require 'hotcocoa'

class TestPlist < Test::Unit::TestCase
  include HotCocoa

  def test_to_plist
    assert_plist(123)
    assert_plist(true)
    assert_plist(false)
    assert_plist('foo')
    assert_plist(:foo, 'foo')
    assert_plist([1,2,3])
    assert_plist({'un' => 1, 'deux' => 2})
  end

  def test_to_plist_with_invalid_objects
    assert_plist(nil, nil)
    assert_plist(Object.new, nil)
    assert_plist(/foo/, nil)
  end

  private

  def assert_plist(val, expected=val)
    assert_equal(expected, read_plist(val.to_plist))
  end
end
