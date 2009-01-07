#!/usr/bin/env macruby

require 'test/unit'
class Test::Unit::TestCase
  class << self
    def it(name, &block)
      define_method("test_#{name}", &block)
    end
  end
end

$: << File.expand_path('../../lib', __FILE__)
require 'objc_ext/ns_user_defaults'

# These tests should probably move to the macruby part of rubyspec once we get to that point.

class TestNSUserDefaults < Test::Unit::TestCase
  it "returns a value for a given key through the #[] reader method" do
    defaults.setValue('foo', forKey: 'key')
    assert_equal 'foo', defaults['key']
  end
  
  it "assigns a value for a given key through the #[]= writer method" do
    defaults['key'] = 'foo'
    assert_equal 'foo', defaults.valueForKey('key')
  end
  
  it "removes an object for a given key with the #delete method" do
    defaults.setValue('foo', forKey: 'key')
    defaults.delete('key')
    assert_nil defaults.valueForKey('key')
  end
  
  private
  
  def defaults
    NSUserDefaults.standardUserDefaults
  end
end