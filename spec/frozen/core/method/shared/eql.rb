require File.dirname(__FILE__) + '/../../../spec_helper'
require File.dirname(__FILE__) + '/../fixtures/classes'

describe :method_equal, :shared => true do
  before(:each) do
    @m = MethodSpecs::Methods.new
    @m2 = MethodSpecs::Methods.new
    @a = MethodSpecs::A.new
  end

  it "returns true if methods are the same" do
    m1 = @m.method(:foo)
    m2 = @m.method(:foo)

    m1.send(@method, m1).should be_true
    m1.send(@method, m2).should be_true
  end

  it "returns true on aliased methods" do
    m1 = @m.method(:foo)
    m2 = @m.method(:bar)

    m1.send(@method, m2).should be_true
  end
  
  ruby_version_is "1.9" do
    it "returns true if the two methods are alises of each other in C" do
      a = String.instance_method(:size)
      b = String.instance_method(:length)
      a.send(@method, b).should be_true
    end
  end

  it "returns false on a method which is neither aliased nor the same method" do
    m1 = @m.method(:foo)
    m2 = @m.method(:zero)
    
    (m1 == m2).should be_false
  end
  
  it "returns false for a method which is not bound to the same object" do
    m1 = @m.method(:foo)
    m2 = @m2.method(:foo)

    a = @a.method(:baz)
    
    m1.send(@method, m2).should be_false
    m1.send(@method, a).should be_false
    m2.send(@method, a).should be_false
  end

  it "returns false if the two methods are bound to the same object but have different bodies" do
    a = MethodSpecs::Eql.instance_method(:different_body)
    b = MethodSpecs::Eql.instance_method(:same_body)
    a.send(@method, b).should be_false
  end

  it "returns false if the two methods are bound to different objects, have different names, but identical bodies" do
    a = MethodSpecs::Eql.instance_method(:same_body_two)
    b = MethodSpecs::Eql2.instance_method(:same_body)
    a.send(@method, b).should be_false
  end

  it "returns false if the two methods are bound to different objects, have the same names, and identical bodies" do
    a = MethodSpecs::Eql.instance_method(:same_body)
    b = MethodSpecs::Eql2.instance_method(:same_body)
    a.send(@method, b).should be_false
  end

  it "returns false if the argument is not a Method object" do
    String.instance_method(:size).send(@method, 7).should be_false
  end

  it "returns false if the argument is an unbound version of self" do
    method(:load).send(@method, method(:load).unbind).should be_false
  end
end
