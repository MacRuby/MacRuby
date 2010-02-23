require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  
  class Delegate
    def initialize(s); @s = s; end
    def current_queue; Dispatch::Queue.current; end
    def takes_block(&block); block.call; end
    def get_number(); sleep 0.01; 42; end
    def set_name(s); sleep 0.01; @s = s; end
    def to_s; @s.to_s; end
  end
  
  describe "Dispatch::Proxy" do
    before :each do
      @delegate_name = "my_delegate"
      @delegate = Delegate.new(@delegate_name)
      @proxy = Dispatch::Proxy.new(@delegate)
    end
    
    describe :new do
      it "returns a Dispatch::Proxy" do
        @proxy.should be_kind_of Dispatch::Proxy
        @proxy.should be_kind_of SimpleDelegator
      end

      it "takes an optional group and queue for callbacks" do
        g = Dispatch::Group.new
        q = Dispatch::Queue.concurrent(:high)
        proxy = Dispatch::Proxy.new(@delegate, g, q)
        proxy.__group__.should == g
        proxy.__queue__.should == q
      end

      it "creates a default Group if none specified" do
        @proxy.__group__.should be_kind_of Dispatch::Group
      end

      it "uses default queue if none specified" do
        @proxy.__queue__.should == Dispatch::Queue.concurrent
      end
    end

    describe :delegate do
      it "should be returned by __getobj__" do
        @proxy.__getobj__.should == @delegate
      end

      it "should be invoked for methods it defines" do
        @proxy.to_s.should == @delegate_name
      end
    end

    describe :method_missing do
      it "runs methods on a private serial queue" do
        q = @proxy.current_queue
        q.label.should =~ /proxy/
      end

      it "should return Synchronously if block NOT given" do
        retval = @proxy.get_number
        retval.should == 42
      end
    
      it "should otherwise return Asynchronous to block, if given" do
        @value = 0
        retval = @proxy.get_number { |v| @value = v }
        @value.should == 0      
        retval.should == nil
        @proxy.__group__.wait
        @value.should == 42      
      end
    end

    describe :__value__ do
      it "should complete work and return delegate" do
        new_name = "nobody"
        @proxy.set_name(new_name) { }
        d = @proxy.__value__
        d.should be_kind_of Delegate
        d.to_s.should == new_name
      end
    end
    
    describe "state" do
      it "should persist for collection objects" do
        actor = Dispatch::Proxy.new([])
        actor.size.should == 0
        actor << :foo
        actor.size.should == 1
        actor[42] = :foo
        actor.size.should == 43        
        actor.should be_kind_of Dispatch::Proxy
      end
      
      it "should NOT persist under assignment" do
        actor = Dispatch::Proxy.new(0)
        actor.should be_kind_of Dispatch::Proxy
        actor += 1
        actor.should_not be_kind_of Dispatch::Proxy
      end
    end

  end  
end