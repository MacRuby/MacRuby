#!/usr/bin/env macruby

require File.expand_path('../../test_helper', __FILE__)

class MacRuby::TestClassConstantLookup < Test::Unit::TestCase
  module Namespace
    class PureRubyClass; end
    module PureRubyModule; end
    SINGLETON = (class << self; self; end)
  end
  
  it "should not find pure Ruby classes in different namespaces" do
    assert_raise(NameError) { PureRubyClass }
  end
  
  it "should find pure Ruby classes in different namespaces if given the correct path" do
    assert_nothing_raised(NameError) { Namespace::PureRubyClass }
  end
  
  it "should not find pure Ruby modules in different namespaces" do
    assert_raise(NameError) { PureRubyModule }
  end
  
  it "should find pure Ruby modules in different namespaces if given the correct path" do
    assert_nothing_raised(NameError) { Namespace::PureRubyModule }
  end
  
  it "should not find pure Ruby class singletons in different namespaces" do
    assert_raise(NameError) { SINGLETON }
  end
  
  it "should find pure Ruby class singletons in different namespaces if given the correct path" do
    assert_nothing_raised(NameError) { Namespace::SINGLETON }
  end
end

class MacRuby::TestKeyedArguments < Test::Unit::TestCase
  before do
    @key, @value = "key", NSObject.new
  end
  
  it "should allow the user to specify each argument by name" do
    dictionary = {}
    dictionary.setValue(@value, forKey: @key)
    
    assert_equal @value, dictionary.valueForKey(@key)
  end
  
  it "should still find a method without keyed arguments, even if the last argument is a hash" do
    obj = NSObject.new
    def obj.method(first, *others)
      [first, others] # stub this method to return all args
    end
    
    assert_equal [:first, []], obj.method(:first)
    assert_equal [:first, [:foo => 'foo']], obj.method(:first, foo: 'foo')
    assert_equal [:first, [:foo => 'foo', :bar => 'bar']], obj.method(:first, foo: 'foo', bar: 'bar')
  end
  
  it "should forward method calls with #performSelector when selector is given as String" do
    dictionary = NSMutableDictionary.performSelector('new')
    dictionary.performSelector('setObject:forKey:', withObject: @value, withObject: @key)
    
    assert_equal @value, dictionary.performSelector('objectForKey:', withObject: @key)
  end
  
  it "should forward method calls with #performSelector when selector is given as Symbol" do
    dictionary = NSMutableDictionary.performSelector(:'new')
    dictionary.performSelector(:'setObject:forKey:', withObject: @value, withObject: @key)
    
    assert_equal @value, dictionary.performSelector(:'objectForKey:', withObject: @key)
  end
  
  it "should forward method calls with #send when selector is given as String" do
    dictionary = NSMutableDictionary.send('new')
    dictionary.send('setObject:forKey:', @value, @key)
    
    assert_equal @value, dictionary.send('objectForKey:', @key)
  end
  
  it "should forward method calls with #send when selector is given as Symbol" do
    dictionary = NSMutableDictionary.send(:new)
    dictionary.send(:'setObject:forKey:', @value, @key)
    
    assert_equal @value, dictionary.send(:'objectForKey:', @key)
  end
end

class MacRuby::TestGeneralSyntax < Test::Unit::TestCase
  it "should find a Objective-C method with varying number of arguments" do
    [
      ['"foo" == "bar"'],
      ['"foo" == %@', 'bar'],
      ['%@ == %@', 'foo', 'bar']
      
    ].each do |arguments|
      assert_equal '"foo" == "bar"', NSPredicate.predicateWithFormat(*arguments).predicateFormat
    end
  end
end

class MacRuby::TestInstanceVariableAccess < Test::Unit::TestCase
  it "should assign an instance variable on an instance of a pure Objective-C class and _not_ garbage collect it" do
    obj = NSObject.alloc.init
    obj.instance_variable_set(:@foo, 'foo')
    GC.start
    
    assert_equal 'foo', obj.instance_variable_get(:@foo)
  end
  
  it "should assign an instance variable on an instance of a CoreFoundation class and _not_ garbage collect it" do
    obj = NSString.alloc.init
    obj.instance_variable_set(:@foo, 'foo')
    GC.start
    
    assert_equal 'foo', obj.instance_variable_get(:@foo)
  end
end

class MacRuby::TestMethodDispatch < Test::Unit::TestCase
  it "should find a method on a pure Objective-C class in the dispatch chain, before a method on a module that is included" do
    objc = ObjectiveC.new(fixture('PureObjCSubclass.m'))
    objc.compile!
    objc.require!
    
    assert_equal "FOO", PureObjCSubclass.alloc.init.require("foo")
  end
  
  it "should find a method on a class in the dispatch chain, before a method on a module that is included" do
    klass = Class.new do
      def require(name)
        name.upcase
      end
    end
    
    assert_equal "FOO", klass.new.require("foo")
  end
  
  it "should find a method on a singleton in the dispatch chain, before a method on a module that is included" do
    obj = NSObject.new
    def obj.require(name)
      name.upcase
    end
    
    assert_equal "FOO", obj.require("foo")
  end
end

class MacRuby::TestPrimitiveTypes < Test::Unit::TestCase
  before do
    objc = ObjectiveC.new(fixture('PureObjCSubclass.m'))
    objc.compile!
    objc.require!
    
    @pure_objc_instance = PureObjCSubclass.alloc.init
  end
  
  include TempDirHelper
  
  it "should be possible to use a Ruby String as dictionary key and pass the dictionary of into Objective-C land" do
    key = String.new("foo")
    value = Object.new
    dictionary = { key => value }
    
    assert_equal value, @pure_objc_instance.test_getObject(dictionary, forKey: key)
  end
end

class MacRuby::TestInstantiation < Test::Unit::TestCase
  it "should instantiate with klass::new" do
    assert_kind_of NSObject, NSObject.new
  end
  
  it "should instantiate with klass::alloc.init" do
    assert_kind_of NSObject, NSObject.alloc.init
  end
end