require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  
  $dispatch_gval = 0
  class Actee
    def initialize(s="default"); @s = s; end
    def delay_set(n); sleep 0.01; $dispatch_gval = n; end
    def to_s; @s; end
  end
  
  describe "Dispatch method" do
    before :each do
      @actee = Actee.new("my_actee")
    end

    describe :label_for do
      it "should return a unique label for any object" do
        s1 = Dispatch.label_for(@actee)
        s2 = Dispatch.label_for(@actee)
        s1.should_not == s2
      end
    end

    describe :queue_for do
      it "should return a unique queue" do
        q1 = Dispatch.queue_for(@actee)
        q2 = Dispatch.queue_for(@actee)
        q1.should_not == q2
      end
    end

    describe :sync do
      it "should execute the block Synchronously" do
        $dispatch_gval = 0
        Dispatch.sync { @actee.delay_set(42) }
        $dispatch_gval.should == 42
      end
    end

    describe :async do
      it "should execute the block Asynchronously" do
        $dispatch_gval = 0
        Dispatch.async(:default) { @actee.delay_set(42) }
        $dispatch_gval.should == 0
        while $dispatch_gval == 0 do; end
        $dispatch_gval.should == 42      
      end
    end
    
    describe :group do
      it "should execute the block with the specified group" do
        $dispatch_gval = 0
        g = Dispatch::Group.new
        Dispatch.group(g) { @actee.delay_set(42) }
        $dispatch_gval.should == 0
        g.wait
        $dispatch_gval.should == 42      
      end
    end

    describe :wrap do
      it "should return an Actor wrapping an instance of a passed class" do
        actor = Dispatch.wrap(Actee)
        actor.should be_kind_of Dispatch::Actor
        actor.to_s.should == "default"
      end

      it "should return an Actor wrapping any other object" do
        actor = Dispatch.wrap(@actee)
        actor.should be_kind_of Dispatch::Actor
        actor.to_s.should == "my_actee"
      end
    end
    
    describe :fork do
      it "should return a Future for tracking execution of the passed block" do
        $dispatch_gval = 0
        g = Dispatch.fork { @actee.delay_set(42) }
        $dispatch_gval.should == 0
        g.should be_kind_of Dispatch::Future
        #g.join
        $dispatch_gval.should == 42      
      end
      
      it "should return a Group for tracking execution of the passed block" do
        $dispatch_gval = 0
        g = Dispatch.fork { @actee.delay_set(42) }
        $dispatch_gval.should == 0
        g.should be_kind_of Dispatch::Group
        #g.wait
        $dispatch_gval.should == 42      
      end
    end
    
  end
end
