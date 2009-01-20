#!/usr/bin/env macruby

require File.expand_path('../../../test_helper', __FILE__)
require 'hotcocoa'

class SampleClass
end

class TestMapper < Test::Unit::TestCase
  
  include HotCocoa::Mappings
  
  it "should have two hash attributes named #bindings and #delegate" do
    assert Mapper.bindings_modules.is_a?(Hash)
    assert Mapper.delegate_modules.is_a?(Hash)
  end
  
  [ :control_class, :builder_method, :control_module,
    :map_bindings, :map_bindings= ].each do |method|
      
    it "should have a #{method} attribute" do
      assert_respond_to(sample_mapper, method)
    end
    
  end
    
  it "should set it's control class on initialization" do
    assert_equal(sample_mapper(true).control_class, SampleClass)
  end
    
  it "should convert from camelcase to underscore" do
    assert sample_mapper.underscore("SampleCamelCasedWord"), 'sample_camel_cased_word'
  end
  
  def test_include_in_class
    m = sample_mapper(true)
    m.include_in_class
    
    assert_equal m.instance_variable_get('@extension_method'), :include
    
    flunk 'Pending.'
  end
  
  def test_each_control_ancestor
    flunk 'Pending.'
  end
  
  def test_map_class
    flunk 'Pending.'
  end
  
  def test_map_instances_of
    flunk 'Pending.'
  end
  
  private
  
  def sample_mapper(flush = false)
    @mapper = nil if flush
    @mapper || Mapper.new(SampleClass)
  end

end
