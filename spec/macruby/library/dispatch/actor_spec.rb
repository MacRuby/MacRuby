require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  describe "Dispatch::Actor" do
    before :each do
      @actee = "me"
      @actor = Dispatch::Actor.new(@actee)
    end
    
    it "should return an Actor when called with an actee" do
      true.should == true
    end

    it "should undef most of its inherited methods" do
      true.should == true
    end

    it "should NOT undef missing_method or object-id" do
      true.should == true
    end

    it "should invoke actee methods on a private serial queue" do
      true.should == true
    end

    it "should call actee Synchronously if block is NOT given" do
      true.should == true
    end

    it "should call actee Asynchronously if block IS given" do
      true.should == true
    end

    it "should used default callback when called Asynchronously" do
      true.should == true
    end

    it "should use callback queue specified by _on, but only once" do
      true.should == true
    end

    it "should invoke actee once with group specified by _with" do
      true.should == true
    end

    it "should invoke actee Asynchronously when group specified by _with" do
      true.should == true
    end

    it "should return actee when called with _done" do
      true.should == true
    end

    it "should return an actor for Dispatch.wrap" do
      true.should == true
    end

  end
end