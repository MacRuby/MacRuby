require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  
  describe "Dispatch::Queue" do
    before :each do
      @my_object = "Hello, World!"
      @q = Dispatch::Queue.for(@my_object)
    end

    describe :labelize do
      it "should return a unique label for any object" do
        s1 = Dispatch::Queue.labelize @my_object
        s2 = Dispatch::Queue.labelize @my_object
        s1.should_not == s2
      end
    end

    describe :for do
      it "should return a dispatch queue" do
        @q.should be_kind_of Dispatch::Queue
      end

      it "should return a unique queue for each object" do
        q = Dispatch::Queue.for(@my_object)
        @q.should_not == q
      end

      it "should return a unique label for each queue" do
        q = Dispatch::Queue.for(@my_object)
        @q.to_s.should_not == q.to_s
      end
    end

    describe :join do
      it "should wait until pending blocks execute " do
        @n = 0
        @q.async {@n = 42}
        @n.should == 0
        @q.join
        @n.should == 42        
      end
    end
  end
end
