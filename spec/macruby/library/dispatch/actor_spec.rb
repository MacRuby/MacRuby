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
    
    describe :new do
      it "should return an Actor when called with an actee" do
        @actor.should be_kind_of(Dispatch::Actor)
        @actor.should be_kind_of(SimpleDelegator)
      end
      
      it "should set callback queue to use when called Asynchronously" do
        qn = Dispatch::Queue.new("custom")
        @actor2 = Dispatch::Actor.new(@actee, qn)
        @qs = ""
        @actor2.increment(41) {|rv| @qs = Dispatch::Queue.current.to_s}
        while @qs == "" do; end
        @qs.should == qn.label
      end

      it "should set default callback queue if not specified" do
        @qs = ""
        @actor.increment(41) {|rv| @qs = Dispatch::Queue.current.to_s}
        while @qs == "" do; end
        @qs.should =~ /com.apple.root.default/
      end
    end

    describe :actee do
      it "should be returned by __getobj__" do
        @actee.should be_kind_of(Actee)
        @actor.__getobj__.should be_kind_of(Actee)
      end

      it "should be invoked for most inherited methods" do
        @actor.to_s.should == @actee.to_s
      end
    end

    describe "method invocation" do
      it "should occur on a private serial queue" do
        q = @actor.current_queue
        q.label.should =~ /dispatch.actor/
      end

      it "should be Synchronous if block is NOT given" do
        $global = 0
        @actor.delay_set(42)
        $global.should == 42
      end
    
      it "should be Asynchronous if block IS given" do
        $global = 0
        @actor.delay_set(42) { }
        $global.should == 0
        while $global == 0 do; end
        $global.should == 42      
      end
    end

    describe "return" do
      it "should be value when called Synchronously" do
        @actor.increment(41).should == 42
      end

      it "should be nil when called Asynchronously" do
        @v = 0
        v = @actor.increment(41) {|rv| @v = rv}
        v.should.nil?
      end

      it "should pass value to block when called Asynchronously" do
        @v = 0
        @actor.increment(41) {|rv| @v = rv}
        while @v == 0 do; end
        @v.should == 42
      end
    end

    describe :_on_ do
      it "should specify callback queue used for actee async" do
        qn = Dispatch::Queue.new("custom")
        @qs = ""
        @actor._on_(qn).increment(41) {|rv| @qs = Dispatch::Queue.current.to_s}
        while @qs == "" do; end
        @qs.should == qn.label
      end
    end

    describe :_with_ do
      it "should specify group used for actee async" do
        $global = 0
        g = Dispatch::Group.new
        @actor._with_(g).delay_set(42)
        $global.should == 0
        g.wait
        $global.should == 42      
      end
    end

    describe :_done_ do
      it "should complete work and return actee" do
        $global = 0
        @actor.delay_set(42) { }
        $global.should == 0
        actee = @actor._done_
        $global.should == 42
        actee.should == @actee     
      end
    end
    
    describe "state" do
      it "should persist for collection objects" do
        actor = Dispatch::Actor.new([])
        actor.size.should == 0
        actor << :foo
        actor.size.should == 1
        actor[42] = :foo
        actor.size.should == 43        
      end
      
      it "should persist for numbers" do
        actor = Dispatch::Actor.new(0)
        actor.to_i.should == 0
        actor += 1
        actor.to_i.should == 1
        actor += 41.0
        actor.to_i.should == 42
        sum = actor - 1
        sum.should == 41
      end
    end

  end  
end