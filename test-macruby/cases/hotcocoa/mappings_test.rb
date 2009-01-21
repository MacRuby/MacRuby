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
  
  it "should create a mapping to a class with a Class instance given to #map" do
    Mappings.map(:klass => SampleClass) {}
    assert_equal SampleClass, Mappings.mappings[:klass].control_class
  end
  
  it "should create a mapping to a class with a string name of the class given to #map" do
    Mappings.map(:klass => 'SampleClass') {}
    assert_equal SampleClass, Mappings.mappings[:klass].control_class
  end
  
  it "should create a mapping to a class with a symbol name of the class given to #map" do
    Mappings.map(:klass => :SampleClass) {}
    assert_equal SampleClass, Mappings.mappings[:klass].control_class
  end
  
  it "should register the key, in the options given to #map, as the builder_method" do
    Mappings.map(:klass => SampleClass) {}
    assert_equal Mappings.mappings[:klass].builder_method, :klass
  end
  
  it "should use the block given to #map as the control_module body" do
    Mappings.map(:klass => SampleClass) do
      def a_control_module_instance_method; end
    end
    
    assert Mappings.mappings[:klass].control_module.
            instance_methods.include?(:a_control_module_instance_method)
  end
  
  it "should create a mapping to a class in a framework with #map" do
    mock = mocked_object
    
    Mappings.map(:klass => 'ClassInTheFrameWork', :framework => 'TheFramework') do
      mock.call!
    end
    Mappings.frameworks["theframework"].last.call
    
    assert mock.called?
  end
  
  it "should execute the framework's callbacks when #framework_loaded is called" do
    mock1, mock2 = mocked_object, mocked_object
    
    [mock1, mock2].each do |mock|
      Mappings.on_framework('TheFramework') { mock.call! }
    end
    Mappings.framework_loaded('TheFramework')
    
    [mock1, mock2].each { |mock| assert mock.called? }
  end
  
  it "should do nothing if the framework passed to #framework_loaded isn't registered" do
    assert_nothing_raised do
      Mappings.framework_loaded('FrameworkDoesNotExist')
    end
  end
  
  def test_reload
    flunk 'Pending.'
  end
  
  private
  
  def mocked_object
    mock = Object.new
    def mock.call!
      @called = true
    end
    def mock.called?
      @called
    end
    mock
  end
end