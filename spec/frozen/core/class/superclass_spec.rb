require File.dirname(__FILE__) + '/../../spec_helper'

describe "Class#superclass" do
  ruby_version_is ""..."1.9" do
    it "returns the superclass of self" do
      Object.superclass.should == nil
      Class.superclass.should == Module
      Class.new.superclass.should == Object
      Class.new(String).superclass.should == String
      Class.new(Fixnum).superclass.should == Fixnum
    end
  end

  ruby_version_is "1.9" do
    it "returns the superclass of self" do
      BasicObject.superclass.should be_nil
      Object.superclass.should == BasicObject
      Class.superclass.should == Module
      Class.new.superclass.should == Object
      Class.new(String).superclass.should == String
      Class.new(Fixnum).superclass.should == Fixnum
    end
  end
end
