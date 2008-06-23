#!/usr/local/bin/macruby

require "test/spec"
require 'mocha'
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
  
  it "should still raise a RuntimeError" do
    lambda { Kernel.framework 'Foo' }.should.raise RuntimeError
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
  
  it "should catch RubyCocoa style method names, call the correct method and define a shortcut method" do
    @obj.perform_selector_with_object('description', nil).should.match /^<TestRubyCocoaStyleMethod/
    @obj.respond_to?(:callMethod_withArgs).should.be true
    @obj.perform_selector_with_object('description', nil).should.match /^<TestRubyCocoaStyleMethod/
  end
  
  it "should also catch RubyCocoa style methods that end with a underscore" do
    @obj.callMethod_withArgs_('description', nil).should.match /^<TestRubyCocoaStyleMethod/
    @obj.respond_to?(:callMethod_withArgs_).should.be true
  end
  
  it "should also work on other regular NSObject subclasses" do
    nsstring = 'foo'
    nsstring.insertString_atIndex('bar', 3)
    nsstring.should == 'foobar'
  end
  
  it "should still raise a NoMethodError if the selector doesn't exist (or rather call the previous implementation)" do
    lambda { @obj.does_not_exist }.should.raise NoMethodError
    lambda { @obj.doesnotexist_ }.should.raise NoMethodError
    lambda { @obj.doesnotexist }.should.raise NoMethodError
  end
  
  it "should be possible to call super methods" do
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
  
  it "should still raise a NameError from OSX not the toplevel object" do
    lambda { OSX::DOES_NOT_EXIST_IN_TOPLEVEL_OBJECT }.should.raise NameError
    
    begin
      OSX::DOES_NOT_EXIST_IN_TOPLEVEL_OBJECT
    rescue NameError => e
      e.message.should.include 'OSX::DOES_NOT_EXIST_IN_TOPLEVEL_OBJECT'
    end
  end
end