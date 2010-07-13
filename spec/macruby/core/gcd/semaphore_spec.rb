# XXX disabled because of <rdar://problem/7472121> dispatch_release() crashes on a semaphore after a successful wait

require File.dirname(__FILE__) + "/../../spec_helper"

if MACOSX_VERSION >= 10.6

  describe "Dispatch::Semaphore" do
    before :each do
      @sema0 = Dispatch::Semaphore.new 0
      @sema1 = Dispatch::Semaphore.new 1
      @q = Dispatch::Queue.new('org.macruby.gcd_spec.semaphore')
    end
    describe :new do
      it "returns an instance of Semaphore for non-negative counts" do
        @sema0.should be_kind_of(Dispatch::Semaphore)
        @sema1.should be_kind_of(Dispatch::Semaphore)
      end

      it "raises an ArgumentError if the count isn't specified" do
        lambda { Dispatch::Semaphore.new }.should raise_error(ArgumentError)
      end

      it "raises a TypeError if a non-numeric count is provided" do
        lambda { Dispatch::Semaphore.new :foo }.should raise_error(TypeError)
        lambda { Dispatch::Semaphore.new 3.5 }.should_not raise_error(TypeError)
        lambda { Dispatch::Semaphore.new "3" }.should raise_error(TypeError)
      end

      it "raises an ArgumentError if the specified count is less than zero" do
        lambda { Dispatch::Semaphore.new -1 }.should raise_error(ArgumentError)
      end
    end

    describe :wait do
      it "always returns true with default timeout FOREVER" do
        @sema1.wait.should == true
      end

      it "returns false if explicit timeout DOES expire" do
        @sema0.wait(0.01).should == false
      end

      it "returns true if explicit timeout does NOT expire" do
        @sema1.wait(Dispatch::TIME_FOREVER).should == true
        q = Dispatch::Queue.new('foo')
        q.async { @sema0.signal }
        @sema0.wait(Dispatch::TIME_FOREVER).should == true
      end
    end

    describe :signal do
      it "returns true if it does NOT wake a thread" do
        @sema0.signal.should == true
        @sema1.signal.should == true
      end

      it "returns false if it DOES wake a thread" do
        @q.async do
          sleep 0.1
          @sema0.signal.should == false
          @sema1.signal.should == true
          @sema1.signal.should == false
        end
        @sema0.wait(Dispatch::TIME_FOREVER)
        @sema1.wait(Dispatch::TIME_FOREVER)
        @q.sync {}
      end
    end

  end
end
