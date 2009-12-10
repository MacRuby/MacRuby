require File.dirname(__FILE__) + "/../../spec_helper"

if MACOSX_VERSION >= 10.6
  
  describe "Dispatch::Semaphore" do
    it "returns an instance of Semaphore" do
      @sema = Dispatch::Semaphore.new 1
      @sema.should be_kind_of(Dispatch::Semaphore)
    end

    it "takes a non-negative Fixnum representing the width of the semaphore" do
      sema0 = Dispatch::Semaphore.new 0
      sema0.should be_kind_of(Dispatch::Semaphore)
      sema10 = Dispatch::Semaphore.new 10
      sema10.should be_kind_of(Dispatch::Semaphore)
    end

    it "raises an ArgumentError if the width isn't specified" do
      lambda { Dispatch::Semaphore.new }.should raise_error(ArgumentError)
    end

    it "raises an TypeError if a non-integer width is provided" do
      lambda { Dispatch::Semaphore.new :foo }.should raise_error(TypeError)
      lambda { Dispatch::Semaphore.new 3.5 }.should raise_error(TypeError)
    end

    it "returns nil object if the specified width is less than zero" do
      sema = Dispatch::Semaphore.new -1
      sema.should.be_kind_of(NilClass)
    end

    it "returns non-zero from 'signal' if it does wake a thread" do
      sema = Dispatch::Semaphore.new 0
      sema.signal.should_not == 0
    end

    it "returns zero from 'signal' if it does NOT wake a thread" do
      sema = Dispatch::Semaphore.new 1
      sema.signal.should == 0
    end

    it "returns non-zero from wait if explicit timeout does expire" do
      sema = Dispatch::Semaphore.new 0
      sema.wait(0.01).should_not == 0 
    end

    it "returns zero from wait if explicit timeout does NOT expire" do
      sema = Dispatch::Semaphore.new 0
      q = Dispatch::Queue.new('foo')
      q.async {sema.signal.should_not == 0 }
      sema.wait(1).should == 0 
    end

    it "always returns zero from wait with default timeout FOREVER" do
      sema = Dispatch::Semaphore.new 0
      q = Dispatch::Queue.new('foo')
      q.async {sema.signal.should_not == 0 }
      sema.wait.should == 0 
    end

  end
end