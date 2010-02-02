require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  
  $global = 0
  class Actee
    def initialize(s); @s = s; end
    def current_queue; Dispatch::Queue.current; end
    def delay_set(n); sleep 0.01; $global = n; end
    def increment(v); v+1; end
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
      $global = 0
      @actor.delay_set(42)
      $global.should == 42
    end

    it "should return value when called Synchronously" do
      @actor.increment(41).should == 42
    end

    it "should call actee Asynchronously if block IS given" do
      $global = 0
      @actor.delay_set(42) { }
      $global.should == 0
      while $global == 0 do; end
      $global.should == 42      
    end

    it "should return nil when called Asynchronously" do
      @v = 0
      v = @actor.increment(41) {|rv| @v = rv}
      v.should.nil?
    end

    it "should provide return value to block when called Asynchronously" do
      @v = 0
      @actor.increment(41) {|rv| @v = rv}
      while @v == 0 do; end
      @v.should == 42
    end

    it "should use default callback queue when called Asynchronously" do
      @qs = ""
      @actor.increment(41) {|rv| @qs = Dispatch::Queue.current.to_s}
      while @qs == "" do; end
      @qs.should =~ /com.apple.root.default/
    end

    it "should use specified callback queue when called Asynchronously" do
      qn = Dispatch::Queue.new("custom")
      @actor2 = Dispatch::Actor.new(@actee, qn)
      @qs = ""
      @actor2.increment(41) {|rv| @qs = Dispatch::Queue.current.to_s}
      while @qs == "" do; end
      @qs.should == qn.label
    end

    it "should use callback queue specified by _on_" do
      qn = Dispatch::Queue.new("custom")
      @qs = ""
      @actor._on_(qn).increment(41) {|rv| @qs = Dispatch::Queue.current.to_s}
      while @qs == "" do; end
      @qs.should == qn.label
    end

    it "should invoke actee async when group specified by _with_" do
      $global = 0
      g = Dispatch::Group.new
      @actor._with_(g).delay_set(42)
      $global.should == 0
      g.wait
      $global.should == 42      
    end

    it "should complete work and return actee when called with _done" do
      $global = 0
      @actor.delay_set(42) { }
      $global.should == 0
      actee = @actor._done_
      $global.should == 42
      actee.should == @actee     
    end
  end
  
end