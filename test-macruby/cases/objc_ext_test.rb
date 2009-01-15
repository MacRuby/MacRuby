#!/usr/bin/env macruby

require File.expand_path('../../test_helper', __FILE__)

framework 'Cocoa'

# These tests should probably move to the macruby part of rubyspec once we get to that point.

require 'objc_ext/ns_user_defaults'

class TestNSUserDefaultsExtensions < Test::Unit::TestCase
  it "should returns a value for a given key through the #[] reader method" do
    defaults.setValue('foo', forKey: 'key')
    assert_equal 'foo', defaults['key']
  end
  
  it "should assign a value for a given key through the #[]= writer method" do
    defaults['key'] = 'foo'
    assert_equal 'foo', defaults.valueForKey('key')
  end
  
  it "should remove an object for a given key with the #delete method" do
    defaults.setValue('foo', forKey: 'key')
    defaults.delete('key')
    assert_nil defaults.valueForKey('key')
  end
  
  private
  
  def defaults
    NSUserDefaults.standardUserDefaults
  end
end

require 'objc_ext/ns_rect'

class TestNSRectExtensions < Test::Unit::TestCase
  def setup
    @rect = NSRect.new([100, 100], [200, 200])
  end
  
  it "should return its size instance's height with #height" do
    assert_equal 200, @rect.height
  end
  
  it "should assign the height to its size instance with #height=" do
    @rect.height = 300
    assert_equal 300, @rect.height
    
    @rect.height = NSNumber.numberWithInt(400)
    assert_equal 400, @rect.height
  end
  
  it "should return its size instance's width with #width" do
    assert_equal 200, @rect.width
  end
  
  it "should assign the width to its size instance with #width=" do
    @rect.width = 300
    assert_equal 300, @rect.width
    
    @rect.width = NSNumber.numberWithInt(400)
    assert_equal 400, @rect.width
  end
  
  it "should return its origin instance's x coord with #x" do
    assert_equal 100, @rect.x
  end
  
  it "should assign the x coord to its origin instance with #x=" do
    @rect.x = 200
    assert_equal 200, @rect.x
    
    @rect.x = NSNumber.numberWithInt(300)
    assert_equal 300, @rect.x
  end
  
  it "should return its origin instance's y coord with #y" do
    assert_equal 100, @rect.y
  end
  
  it "should assign the y coord to its origin instance with #y=" do
    @rect.y = 200
    assert_equal 200, @rect.y
    
    @rect.y = NSNumber.numberWithInt(300)
    assert_equal 300, @rect.y
  end
end