require File.dirname(__FILE__) + '/../../spec_helper'
require 'enumerator'

describe "Enumerator#rewind" do

  ruby_version_is "1.8.7" do  
    before(:each) do
      @enum = enumerator_class.new(1, :upto, 3)
    end  

    it "resets the enumerator to its initial state" do
      @enum.next.should == 1
      @enum.next.should == 2
      @enum.rewind
      @enum.next.should == 1
    end

    it "returns self" do
      @enum.rewind.should == @enum
    end

    it "has no effect on a new enumerator" do
      @enum.rewind
      @enum.next.should == 1
    end

    it "has no effect if called multiple, consecutive times" do
      @enum.next.should == 1
      @enum.rewind
      @enum.rewind
      @enum.next.should == 1
    end
    
    ruby_version_is "1.9" do
      it "calls the enclosed object's rewind method if one exists" do
        obj = mock('rewinder')
        enum = enumerator_class.new(obj, :enum) 
        obj.should_receive(:rewind)
        enum.rewind
      end

      it "does nothing if the object doesn't have a #rewind method" do
        obj = mock('rewinder')
        enum = enumerator_class.new(obj) 
        lambda { enum.rewind.should == enum }.should_not raise_error
      end
    end
  end    
end
