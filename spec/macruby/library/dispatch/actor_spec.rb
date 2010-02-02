require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  
  class Actee
    def initialize(s); @s = s; end
    def current_queue; Dispatch::Queue.current; end
    def wait(n); sleep n; end
    def to_s; @s; end
  end
  
  describe "Dispatch::Actor" do
    before :each do
      @actee = Actee.new("my_actee")
      @actor = Dispatch::Actor.new(@actee)
    end
    
    it "should return an Actor when called with an actee" do
      @actor.should be_kind_of(Dispatch::Actor)
      @actor.should be_kind_of(SimpleDelegator)
    end

    it "should return the actee when called with __getobj__" do
      @actee.should be_kind_of(Actee)
      @actor.__getobj__.should be_kind_of(Actee)
    end

    it "should invoke actee for most inherited methods" do
      @actor.to_s.should == @actee.to_s
    end

    it "should invoke actee methods on a private serial queue" do
      q = @actor.current_queue
      q.label.should =~ /dispatch.actor/
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