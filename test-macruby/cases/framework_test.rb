#!/usr/bin/env macruby

require File.expand_path('../../test_helper', __FILE__)

class TestFramework < Test::Unit::TestCase

  def test_setup
    framework('Foundation')
  end

  def test_framework_by_name
    assert_nothing_raised { framework('Foundation') }
    assert_raise(RuntimeError) { framework('DoesNotExist') }
  end

  def test_framework_by_path
    assert_nothing_raised do 
      framework('/System/Library/Frameworks/Foundation.framework')
    end
    assert_raise(RuntimeError) { framework('/does/not/exist') }
  end

  def test_framework_load_twice
    assert_equal(false, framework('Foundation'))
    assert_equal(false,
      framework('/System/Library/Frameworks/Foundation.framework'))
  end

end
