#!/usr/local/bin/macruby

require "test/spec"
require 'mocha'

#require File.expand_path('../../lib/osx/cocoa', __FILE__)
require 'osx/cocoa'

class TestRubyCocoaStyleMethod < OSX::NSObject
  def initialize
    @set_from_initialize = 'represent!'
  end
  
  def perform_selector_with_object(sel, obj)
    callMethod_withArgs(sel, obj)
  end
  
  def callMethod(method, withArgs:args)
    send(method, *args)
  end
  
  def method_rubyCocoaStyle_withExtraArg(mname, rc_style, arg)
    "#{mname}(#{rc_style}, #{arg})"
  end
  
  def method_notRubyCocoaStyle(first_arg, second_arg, third_arg)
  end
  
  def self.classMethod(mname, macRubyStyle:mr_style, withExtraArg:arg)
    "#{mname}(#{mr_style}, #{arg})"
  end
end

class TestRubyCocoaStyleSuperMethod < OSX::NSObject
  def init
    self if super_init
  end
end

class NSObjectSubclassWithInitialize < OSX::NSObject
  def initialize
    @set_from_initialize = 'represent!'
  end
end

class NSObjectSubclassWithoutInitialize < OSX::NSObject; end

CONSTANT_IN_THE_TOPLEVEL_OBJECT_INSTEAD_OF_OSX = true

describe "RubyCocoa layer, in general" do
  xit "should load AppKit when the osx/cocoa file is loaded" do
    Kernel.expects(:framework).with('AppKit')
    load 'osx/cocoa.rb'
  end
  
  it "should set a global variable which indicates that a framework is being loaded" do
    Kernel.expects(:__framework_before_rubycocoa_layer).with do |f|
      $LOADING_FRAMEWORK.should.be true
      f == 'Foo'
    end
    Kernel.framework 'Foo'
    $LOADING_FRAMEWORK.should.be false
  end
end

describe "NSObject additions" do
  before do
    @obj = TestRubyCocoaStyleMethod.alloc.init
  end
  
  it "should alias ib_outlet to ib_outlets" do
    TestRubyCocoaStyleMethod.private_methods.should.include :ib_outlets
  end
  
  it "should call initialize from init if it exists" do
    NSObjectSubclassWithInitialize.alloc.init.instance_variable_get(:@set_from_initialize).should == 'represent!'
    NSObjectSubclassWithoutInitialize.alloc.init.instance_variable_get(:@set_from_initialize).should.be nil
  end
  
  it "should catch RubyCocoa style instance method, call the correct MacRuby style method and define a shortcut method" do
    @obj.perform_selector_with_object('description', nil).should.match /^<TestRubyCocoaStyleMethod/
    @obj.respond_to?(:callMethod_withArgs).should.be true
    @obj.perform_selector_with_object('description', nil).should.match /^<TestRubyCocoaStyleMethod/
  end
  
  it "should catch RubyCocoa style instance methods that end with a underscore" do
    @obj.callMethod_withArgs_('description', nil).should.match /^<TestRubyCocoaStyleMethod/
    @obj.respond_to?(:callMethod_withArgs_).should.be true
  end
  
  it "should catch RubyCocoa style instance methods defined in Objective-C" do
    color = NSColor.colorWithCalibratedRed(1.0, green:1.0, blue:1.0, alpha:1.0)
    lambda { color.blendedColorWithFraction_ofColor(0.5, NSColor.greenColor) }.should.not.raise NoMethodError
  end
  
  it "should catch RubyCocoa style class methods defined in ruby" do
    lambda { TestRubyCocoaStyleMethod.classMethod_macRubyStyle_withExtraArg(1, 2, 3) }.should.not.raise NoMethodError
    TestRubyCocoaStyleMethod.respond_to?(:classMethod_macRubyStyle_withExtraArg).should.be true
  end
  
  it "should catch RubyCocoa style class methods defined in Objective-C" do
    lambda { NSColor.colorWithCalibratedRed_green_blue_alpha(1.0, 1.0, 1.0, 1.0) }.should.not.raise NoMethodError
    NSColor.respond_to?(:colorWithCalibratedRed_green_blue_alpha).should.be true
  end
  
  it "should still raise NoMethodError if a class method doesn't exist" do
    lambda { NSColor.colorWithCalibratedRed_pink(1.0, 1.0) }.should.raise NoMethodError
  end
  
  it "should also work on other regular NSObject subclasses" do
    nsstring = 'foo'
    nsstring.insertString_atIndex('bar', 3)
    nsstring.should == 'foobar'
  end
  
  it "should still raise a NoMethodError if the selector doesn't exist when a method is missing" do
    lambda { @obj.does_not_exist }.should.raise NoMethodError
    lambda { @obj.doesnotexist_ }.should.raise NoMethodError
    lambda { @obj.doesnotexist }.should.raise NoMethodError
  end
  
  it "should be possible to call super_foo type methods" do
    lambda { TestRubyCocoaStyleSuperMethod.alloc.init }.should.not.raise.exception
  end
  
  it "should handle objc_send style methods" do
    nsstring = 'foo'
    nsstring.objc_send(:insertString, 'bar', :atIndex, 3)
    nsstring.should == 'foobar'
    nsstring.objc_send(:description).should == 'foobar'
    lambda { nsstring.objc_send(:does_not_exist) }.should.raise NoMethodError
  end
  
  it "should create MacRuby style method aliases for any method containing underscores" do
    @obj.respond_to?(:"method:rubyCocoaStyle:withExtraArg:").should.be true
    @obj.method('foo', rubyCocoaStyle:true, withExtraArg:false).should == 'foo(true, false)'
  end
  
  it "should not create MacRuby style method aliases for methods containing underscores if the arity doesn't match" do
    @obj.respond_to?(:"method:notRubyCocoaStyle:").should.be false
  end
  
  it "should respond to to_ruby" do
    @obj.respond_to?(:to_ruby).should.be true
  end
end

describe 'OSX module' do
  it "should exist" do
    defined?(OSX).should.not.be nil # this is weird.. I haven't defined OSX yet?!
  end
  
  it "should load a framework into the runtime" do
    OSX.require_framework 'WebKit'
    defined?(WebView).should.not.be nil
    
    OSX.require_framework '/System/Library/Frameworks/QTKit.framework'
    defined?(QTMovie).should.not.be nil
  end
  
  it "should forward messages to Kernel if it responds to it" do
    Kernel.expects(:NSRectFill).with(1, 2).times(2)
    OSX::NSRectFill(1, 2)
    OSX.respond_to?(:NSRectFill).should.be true
    OSX::NSRectFill(1, 2)
    
    lambda { OSX::NSRectFillllllll(1, 2) }.should.raise NoMethodError
  end
  
  it "should try to get missing constants from the toplevel object" do
    OSX::CONSTANT_IN_THE_TOPLEVEL_OBJECT_INSTEAD_OF_OSX.should.be true
  end
  
  it "should still raise a NameError from OSX, not from the toplevel object, when a constant is missing" do
    lambda { OSX::DOES_NOT_EXIST_IN_TOPLEVEL_OBJECT }.should.raise NameError
    
    begin
      OSX::DOES_NOT_EXIST_IN_TOPLEVEL_OBJECT
    rescue NameError => e
      e.message.should.include 'OSX::DOES_NOT_EXIST_IN_TOPLEVEL_OBJECT'
    end
  end
end

describe 'NSData additions' do
  before do
    path = '/System/Library/DTDs/BridgeSupport.dtd'
    @obj = NSData.dataWithContentsOfFile(path)
    @ruby_data = File.read(path)
  end

  it "should respond to rubyString" do
    @obj.respond_to?(:rubyString).should.be true
    @obj.rubyString.should.equal @ruby_data
  end
end

describe 'NSUserDefaults additions' do
  before do
    @obj = NSUserDefaults.alloc.init
  end
  
  it "should respond to [] and []=" do
    @obj.respond_to?(:[]).should.be true
    @obj.respond_to?(:[]=).should.be true
    @obj['key'] = 'value'
    @obj['key'].should.equal 'value'
  end
end

describe 'NSIndexSet additions' do
  before do
    @obj = NSIndexSet.alloc.init
  end
  
  it "should respond to to_a" do
    @obj.respond_to?(:to_a).should.be true
  end
end

describe 'NSNumber additions' do
  before do
    @i = NSNumber.numberWithInt(42)
    @f = NSNumber.numberWithDouble(42.42)
  end
  
  it "should respond to to_i and to_f" do
    @i.respond_to?(:to_i).should.be true
    @i.respond_to?(:to_f).should.be true
    @i.to_i.should.equal 42
    @f.to_f.should.equal 42.42
  end
end

describe 'NSDate additions' do
  before do
    @obj = NSDate.alloc.init
  end
  
  it "should respond to to_time" do
    @obj.respond_to?(:to_time).should.be true
  end
end

describe 'NSImage additions' do
  before do
    @obj = NSImage.alloc.init
  end
  
  it "should respond to focus" do
    @obj.respond_to?(:focus).should.be true
  end
end
