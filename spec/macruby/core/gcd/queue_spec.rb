require File.dirname(__FILE__) + "/../../spec_helper"

#TODO: Dispatch::Queue.main.run (without killing spec runner!)

if MACOSX_VERSION >= 10.6
  describe "Dispatch::Queue" do
    before :each do
      @q = Dispatch::Queue.new('org.macruby.gcd_spec.queue')
    end

    describe :concurrent do
      it "returns an instance of Queue" do
        q = Dispatch::Queue.concurrent
        q.should be_kind_of(Dispatch::Queue)
      end

      it "can accept a symbol argument which represents the priority" do
        q1 = Dispatch::Queue.concurrent(:low)
        q1.should be_kind_of(Dispatch::Queue)

        q2 = Dispatch::Queue.concurrent(:default)
        q2.should be_kind_of(Dispatch::Queue)

        q3 = Dispatch::Queue.concurrent(:high)
        q3.should be_kind_of(Dispatch::Queue)
        
        if MACOSX_VERSION >= 10.7
          q4 = Dispatch::Queue.concurrent(:background)
          q4.should be_kind_of(Dispatch::Queue)
        end
      end
      
      if MACOSX_VERSION >= 10.7
        it "can accept a string to create a concurrent queue" do
          q = Dispatch::Queue.concurrent("org.macruby.concurrent.testing")
          q.should be_kind_of(Dispatch::Queue)
        end
      end

      it "raises an ArgumentError if the argument is not a valid priority" do
        lambda { Dispatch::Queue.concurrent(:foo) }.should  raise_error(ArgumentError)
      end

      it "should return the same queue object across invocations" do
        qa = Dispatch::Queue.concurrent(:low)
        qb = Dispatch::Queue.concurrent(:low)
        qa.should eql(qb)
      end

      it "raises a TypeError if the provided priority is not a symbol or string" do
        lambda { Dispatch::Queue.concurrent(42) }.should raise_error(TypeError)
      end
    end

    describe :current do
      it "returns an instance of Queue" do
        @q.should be_kind_of(Dispatch::Queue)
      end

      it "should return the parent queue when inside an executing block" do
        @q2 = nil
        @q.async do
          @q2 = Dispatch::Queue.current
        end
        @q.sync {}
        @q.label.should == @q2.label
      end
    end

    describe :main do
      it "returns an instance of Queue" do
        o = Dispatch::Queue.main
        @q.should be_kind_of(Dispatch::Queue)
      end
    end

    describe :new do
      it "accepts a name and returns an instance of Queue" do
        @q.should be_kind_of(Dispatch::Queue)

        lambda { Dispatch::Queue.new('foo', 42) }.should raise_error(ArgumentError)
        lambda { Dispatch::Queue.new(42) }.should raise_error(TypeError)
      end

      it "raises an ArgumentError if not passed a string" do
        lambda { Dispatch::Queue.new() }.should raise_error(ArgumentError)
      end
    end

    describe :async do
      it "accepts a block and yields it asynchronously" do
        @i = 0
        @q.async { sleep 0.01; @i = 42 }
        @i.should == 0
        while @i == 0 do; end
        @i.should == 42
      end

      it "accepts a group which tracks the asynchronous execution" do
        @i = 0
        g = Dispatch::Group.new
        @q.async(g) { sleep 0.01; @i = 42 }
        @i.should == 0
        g.wait
        @i.should == 42
      end

      it "raises an ArgumentError if no block is given" do
        lambda { @q.async }.should raise_error(ArgumentError) 
      end
    end

    describe :sync do
      it "accepts a block and yields it synchronously" do
        @i = 0
        @q.sync { sleep 0.01; @i = 42 }
        @i.should == 42
      end

      it "raises an ArgumentError if no block is given" do
        lambda { @q.sync }.should raise_error(ArgumentError) 
      end
    end
    
    if MACOSX_VERSION >= 10.7
      describe :barrier_async do
        it "provides a barrier block asynchronously" do
          @i = ""
          cq = Dispatch::Queue.concurrent("org.macruby.testing")
          cq.async { @i += "a" }
          cq.async { @i += "b" }
          cq.barrier_async { @i += "c" }
          sleep 0.1
          @i.length.should == 3
          @i[2].should == "c"
        end
      end
      
      describe :barrier_sync do
        it "provides a synchronous barrier block" do
          @i = ""
          cq = Dispatch::Queue.concurrent("org.macruby.testing")
          cq.async { @i += "a" }
          cq.async { @i += "b" }
          cq.barrier_sync { @i += "c"}
          @i.length.should == 3
          @i[2].should == "c"
        end
      end
      
    end

    describe :apply do
      it "accepts a count and a block and yields it that many times, with an index" do
        @i = 0
        @q.apply(10) { |j| @i += j }
        @i.should == 45
        @i = 42
        @q.apply(0) { |j| @i += 1 }
        @i.should == 42

        lambda { @q.apply(nil) {} }.should raise_error(TypeError) 
      end

      it "raises an ArgumentError if no block is given" do
        lambda { @q.apply(42) }.should raise_error(ArgumentError) 
      end
    end

    describe :after do
      it "accepts a given delay (in seconds) and a block and yields it after" do
        [0.02].each do |delay|

          t = Time.now
          @q.after(delay) { @i = 42 }
          @i = 0
          while @i == 0 do; end
          @i.should == 42
          t2 = Time.now - t
          t2.should > delay
          t2.should < delay*2
        end
      end

      it "runs immediately if nil delay is given" do
        @i = 0
        @q.after(nil) { @i = 42 }
        @q.sync {}
        @i.should == 42        
      end

      it "raises TypeError if no number is given" do
        lambda { @q.after("string") {} }.should raise_error(TypeError) 
      end

      it "raises an ArgumentError if no block is given" do
        lambda { @q.after(42) }.should raise_error(ArgumentError) 
      end
    end

    describe :to_s do
      it "returns the name of the queue" do
        @q.to_s.should == 'org.macruby.gcd_spec.queue'

        qm = Dispatch::Queue.main
        qm.to_s.should == 'com.apple.main-thread'
      end
    end

    describe :suspend! do
      it "suspends the queue which can be resumed by calling #resume!" do
        @q.async { sleep 1 }
        @q.suspended?.should == false
        @q.suspend! 
        @q.suspended?.should == true
        @q.resume!
        @q.suspended?.should == false
      end
    end

  end
end