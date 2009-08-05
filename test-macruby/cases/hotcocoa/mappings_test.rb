#!/usr/bin/env macruby

require File.expand_path('../../../test_helper', __FILE__)
require 'hotcocoa'

class SampleClass
end

class Mock
  def call!
    @called = true
  end
  
  def called?
    @called
  end
end

class TestMappings < Test::Unit::TestCase
  
  include HotCocoa
  
  after do
    Mappings.mappings[:klass] = nil
    Mappings.frameworks["theframework"] = nil
    Mappings.loaded_frameworks.delete('theframework')
  end
  
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
    mock = Mock.new
    
    Mappings.map(:klass => 'SampleClass', :framework => 'TheFramework') do
      mock.call!
    end
    Mappings.frameworks["theframework"].last.call
    
    assert mock.called?
  end
  
  it "should execute the framework's callbacks when #framework_loaded is called" do
    mocks = Array.new(2) { Mock.new }
    
    mocks.each do |mock|
      Mappings.on_framework('TheFramework') { mock.call! }
    end
    Mappings.framework_loaded('TheFramework')
    
    mocks.each { |mock| assert mock.called? }
  end
  
  it "should do nothing if the framework passed to #framework_loaded isn't registered" do
    assert_nothing_raised do
      Mappings.framework_loaded('FrameworkDoesNotExist')
    end
  end
  
  it "should resolve a constant when a framework, that's registered with #map, is loaded" do
    assert_nothing_raised(NameError) do
      Mappings.map(:klass => 'ClassFromFramework', :framework => 'TheFramework') {}
    end
    
    # The mapping should not yet exist
    assert_nil Mappings.mappings[:klass]
    
    # now we actually define the class and fake the loading of the framework
    eval "class ::ClassFromFramework; end"
    Mappings.framework_loaded('TheFramework')
    
    # It should be loaded by now
    assert_equal ClassFromFramework, Mappings.mappings[:klass].control_class
  end
  
  it "should keep a unique list of loaded_frameworks" do
    assert_difference("Mappings.loaded_frameworks.length", +1) do
      Mappings.framework_loaded('TheFramework')
      Mappings.framework_loaded('TheFramework')
    end
    
    assert Mappings.loaded_frameworks.include?('theframework')
  end
  
  it "should return whether or not a framework has been loaded yet" do
    Mappings.framework_loaded('TheFramework')
    assert Mappings.loaded_framework?('TheFramework')
    
    assert !Mappings.loaded_framework?('IHasNotBeenLoaded')
    assert !Mappings.loaded_framework?(nil)
    assert !Mappings.loaded_framework?('')
  end
  
  def test_reload
    flunk 'Pending.'
  end
end