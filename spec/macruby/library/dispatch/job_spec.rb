require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  describe "Dispatch::Job" do
    
    before :each do
      @result = 0
      @job = Dispatch::Job.new
    end

    describe :new do
      it "should return a Job for tracking execution of passed blocks" do
        @job.should be_kind_of Dispatch::Job
      end

      it "should use default queue" do
        @job.add { Dispatch::Queue.current }
        @job.value.to_s.should == Dispatch::Queue.concurrent.to_s
      end

      it "should take an optional queue" do
        q = Dispatch::Queue.concurrent(:high)
        job = Dispatch::Job.new(q) { Dispatch::Queue.current }
        job.value.to_s.should == q.to_s
      end
    end
    
    describe :group do
      it "should return an instance of Dispatch::Group" do
        @job.group.should be_kind_of Dispatch::Group
      end
    end

    describe :values do
      it "should return an instance of Dispatch::Proxy" do
        @job.values.should be_kind_of Dispatch::Proxy
      end

      it "has a __value__ that is Enumerable" do
        @job.values.__value__.should be_kind_of Enumerable
      end
    end

    describe :add do
      it "should schedule a block for async execution" do
        @value = 0
        @job.add { sleep 0.01; @value = 42 }
        @value.should == 0
        @job.join
        @value.should == 42
      end
    end
    
    describe :join do
      it "should wait when called Synchronously" do
        @value = 0
        @job.add { sleep 0.01; @value = 42 }
        @job.join
        @value.should == 42
      end

      it "should invoke passed block Asynchronously" do
        @value = 0
        @rval = 0
        @job.add { sleep 0.01; @value = 42 }
        q = Dispatch::Queue.for(@value)
        @job.join(q) { sleep 0.01; @rval = @value }
        @job.join
        @rval.should == 0
        q.sync { }
        @rval.should == 42
      end
    end

    describe :value do
      it "should return value when called Synchronously" do
        @job.add { Math.sqrt(2**10) }
        @job.value.should == 2**5
      end

      it "should invoke passed block Asynchronously with return value" do
        @job.add { Math.sqrt(2**10) }
        @value = 0
        q = Dispatch::Queue.for(@value)
        @job.value(q) {|v| @value = v}
        @job.join
        q.sync { }
        @value.should == 2**5
      end
    end

    describe :synchronize do
      it "should return serialization Proxy for passed object" do
        actee = {}
        actor = @job.sync(actee)
        actor.should be_kind_of Dispatch::Proxy
        actor.__value__.should == actee
      end

    end

  end
end