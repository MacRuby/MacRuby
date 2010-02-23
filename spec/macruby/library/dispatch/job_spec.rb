require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  describe "Dispatch::Job" do
    
    before :each do
      @result = 0
      @job = Dispatch::Job.new { sleep 0.01; @result = Math.sqrt(2**10) }
    end

    describe :new do
      it "should return a Job for tracking execution of the passed block" do
        @job.should be_kind_of Dispatch::Job
      end

      it "should take a +priority+ for which concurrent queue to use" do
        Job = Dispatch::Job.new(:high) { @result=Dispatch::Queue.current }
        Job.join
        @result.to_s.should == Dispatch::Queue.concurrent(:high).to_s
      end
    end

    describe :group do
      it "should return an instance of Dispatch::Group" do
        @job.group.should be_kind_of Dispatch::Group
      end
    end

    describe :join do
      it "should wait until execution is complete if no block is passed" do
        @q.async(@g) { @i = 42 }
        @g.join
        @i.should == 42
      end

      it "should run a notify block on completion, if passed" do
        @q.async(@g) { @i = 42 }
        @g.join {@i *= 2}
        while @i <= 42 do; end    
        @i.should == 84
      end

      it "should run notify block on specified queue, if any" do
        @q.async(@g) { @i = 42 }
        @q.sync { }
        @g.join(@q) {@i *= 2}
        @q.sync { }
        @i.should == 84
      end
    end

    describe :value do
      it "should return value when called Synchronously" do
        @job.value.should == 2**5
      end

      it "should invoke passed block Asynchronously with return value" do
        @fval = 0
        @job.value {|v| @fval = v}
        while @fval == 0 do; end
        @fval.should == 2**5
      end
    end
    
  end
end