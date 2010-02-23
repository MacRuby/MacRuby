require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  
  describe "Dispatch::Queue" do
    before :each do
      @my_object = "Hello, World!"
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
        q = Dispatch::Queue.for(@my_object)
        q.should be_kind_of Dispatch::Queue
      end

      it "should return a unique queue for each object" do
        q1 = Dispatch::Queue.for(@my_object)
        q2 = Dispatch::Queue.for(@my_object)
        q1.should_not == q2
      end

      it "should return a unique label for each queue" do
        q1 = Dispatch::Queue.for(@my_object)
        q2 = Dispatch::Queue.for(@my_object)
        q1.to_s.should_not == q2.to_s
      end
    end
  end
end
