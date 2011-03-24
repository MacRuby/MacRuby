#!/usr/bin/env macruby

require File.expand_path('../../../test_helper', __FILE__)
require 'hotcocoa'

module TestNamespaceForConstLookup
  def self.const_missing(const)
    @missing_const = const
  end
  
  def self.missing_const
    @missing_const
  end
end

class TestObjectExt < Test::Unit::TestCase
  it 'should return a constant by FQ name _in_ receiver namespace' do
    assert_equal HotCocoa,           Object.full_const_get("HotCocoa")
    assert_equal HotCocoa::Mappings, Object.full_const_get("HotCocoa::Mappings")
  end
  
  it "should call ::const_missing on the namespace which _does_ exist" do
    Object.full_const_get('TestNamespaceForConstLookup::DoesNotExist')
    assert_equal 'DoesNotExist', TestNamespaceForConstLookup.missing_const
  end
  
  it "should normally raise a NameError if a const cannot be found" do
    assert_raise(NameError) do
      Object.full_const_get('DoesNotExist::ForSure')
    end
  end
end