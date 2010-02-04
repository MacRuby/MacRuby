require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  
  $global = 0
  class Actee
    def initialize(s="default"); @s = s; end
    def delay_set(n); sleep 0.01; $global = n; end
    def to_s; @s; end
  end
  
  describe "Dispatch method" do
    before :each do
      @actee = Actee.new("my_actee")
    end

    describe :queue_for do
      it "should return a unique label per actee" do
        s1 = Dispatch.queue_for(@actee).to_s
        s2 = Dispatch.queue_for(@actee).to_s
        s3 = Dispatch.queue_for(Actee.new("your_actee")).to_s
        s1.should == s2
        s1.should_not == s3
      end
    end

    describe :sync do
      it "should execute the block Synchronously" do
        $global = 0
        Dispatch.sync { @actee.delay_set(42) }
        $global.should == 42
      end
    end

    describe :async do
      it "should execute the block Asynchronously" do
        $global = 0
        Dispatch.async(:default) { @actee.delay_set(42) }
        $global.should == 0
        while $global == 0 do; end
        $global.should == 42      
      end
    end
    
    describe :group do
      it "should execute the block with the specified group" do
        $global = 0
        g = Dispatch::Group.new
        Dispatch.group(g) { @actee.delay_set(42) }
        $global.should == 0
        g.wait
        $global.should == 42      
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
      it "should return an Group for tracking execution of the passed block" do
        $global = 0
        g = Dispatch.fork { @actee.delay_set(42) }
        $global.should == 0
        g.should be_kind_of Dispatch::Group
        g.wait
        $global.should == 42      
      end

      it "should :join Synchronously to that group" do
        $global = 0
        g = Dispatch.fork { @actee.delay_set(42) }
        $global.should == 0
        g.join
        $global.should == 42      
      end

      it "should :join Asynchronously if passed another block" do
        $global = 0
        g = Dispatch.fork { @actee.delay_set(42) }
        $global.should == 0
        g.join { }
        $global.should == 0
        while $global == 0 do; end
        $global.should == 42      
      end
    end
    
  end
end
