#!/usr/bin/env macruby

require File.expand_path('../../../test_helper', __FILE__)
require 'hotcocoa'

class SampleClass
  def self.val; @val || false; end
  def self.val= (v); @val = v; end
end

class TestMappings < Test::Unit::TestCase
  
  include HotCocoa
    
  it "should have two Hash attributes named #mappings and #frameworks" do
    assert Mappings.mappings.is_a?(Hash)
    assert Mappings.frameworks.is_a?(Hash)
  end
  
  it "should register callbacks for the point of time when a framework is loaded with #on_framework" do
    p = Proc.new {}
    Mappings.on_framework(:Foo, &p)
    assert_equal(Mappings.frameworks['foo'].last, p)
  end
  
  it "should create a mapping to a class with #map" do
    
    block = Proc.new do
      def alloc_with_options(options); options; end
    end
    
    HotCocoa::Mappings.map({:foo => :SampleClass}, &block)
    
    m = HotCocoa::Mappings.mappings[:foo]
    
    assert_equal(m.control_class, SampleClass)
    assert_equal(m.builder_method, :foo)
    assert(m.control_module.instance_methods.include?(:alloc_with_options))
    
  end
  
  it "should create a mapping to a class for a framework with #map" do
    p = Proc.new {}
    HotCocoa::Mappings.map({:foo => :SampleClass, :framework => :Anonymous}, &p)
    # require 'pp'; pp HotCocoa::Mappings
    
    # FIXME: This is not really nice. We test that the result exists, but not what it is.
    assert_equal Mappings.frameworks["anonymous"].size, 1
  end
  
  it "should call the framework's callbacks if it's passed to #framework_loaded" do
    p = Proc.new { SampleClass.val = true }
    Mappings.on_framework(:Foo, &p)
    Mappings.framework_loaded(:Foo)
    
    assert_equal SampleClass.val, true
  end
  
  it "should raise nothing if there's no entry for the framework passed to #framework_loaded" do
    assert_nothing_raised do
      Mappings.framework_loaded(:FrameworkDoesNotExist)
    end
  end
  
  def test_reload
    flunk 'Pending.'
  end
  
end