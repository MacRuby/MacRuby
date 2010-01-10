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
      end

      it "raises an ArgumentError if the argument is not a valid priority" do
        lambda { Dispatch::Queue.concurrent(:foo) }.should  raise_error(ArgumentError)
      end

      it "should return the same queue object across invocations" do
        qa = Dispatch::Queue.concurrent(:low)
        qb = Dispatch::Queue.concurrent(:low)
        qa.should eql(qb)
      end

      it "raises a TypeError if the provided priority is not a symbol" do
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
        @q.async { @i = 42 }
        while @i == 0 do; end
        @i.should == 42
      end


      it "raises an ArgumentError if no block is given" do
        lambda { @q.async }.should raise_error(ArgumentError) 
      end
    end

    describe :sync do
      it "accepts a block and yields it synchronously" do
        @i = 0
        @q.sync { @i = 42 }
        @i.should == 42
      end

      it "raises an ArgumentError if no block is given" do
        lambda { @q.sync }.should raise_error(ArgumentError) 
      end
    end

    describe :apply do
      it "accepts an input size and a block and yields it as many times" do
        @i = 0
        @q.apply(10) { @i += 1 }
        @i.should == 10
        @i = 42
        @q.apply(0) { @i += 1 }
        @i.should == 42

        lambda { @q.apply(nil) {} }.should raise_error(TypeError) 
      end

      it "raises an ArgumentError if no block is given" do
        lambda { @q.apply(42) }.should raise_error(ArgumentError) 
      end
    end

    describe :after do
      it "accepts a given time (in seconds) and a block and yields it after" do
        [0.1].each do |test_time|

          t = Time.now
          @q.after(test_time) { @i = 42 }
          @i = 0
          while @i == 0 do; end
          @i.should == 42
          t2 = Time.now - t
          t2.should > test_time
          t2.should < test_time*2
        end
      end

      it "raises an ArgumentError if no time is given" do
        lambda { @q.after(nil) {} }.should raise_error(TypeError) 
      end

      it "raises an ArgumentError if no block is given" do
        lambda { @q.after(42) }.should raise_error(ArgumentError) 
      end
    end

    describe :label, :shared => true do
      it "returns the name of the queue" do
        @q.label.should == 'org.macruby.gcd_spec.queue'

        qm = Dispatch::Queue.main
        qm.label.should == 'com.apple.main-thread'
      end
    end    

    describe :to_s do
      it_should_behave_like :label
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