require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  describe "Dispatch::Future" do
    
    before :each do
      @result = 0
      @future = Dispatch::Future.new { sleep 0.01; @result = Math.sqrt(2**10) }
    end

    describe :new do
      it "should return a Future for tracking execution of the passed block" do
        @future.should be_kind_of Dispatch::Future
      end

      it "should return an instance of Dispatch::Group" do
        @future.should be_kind_of Dispatch::Group
      end
    end

    describe :join do
      it "should wait until execution is complete" do
        @result.should == 0
        @future.join
        @result.should == 2**5
      end
    end

    describe :value do
      it "should return value when called Synchronously" do
        @future.value.should == 2**5
      end

      it "should invoke passed block Asynchronously with return value" do
        @fval = 0
        @future.value {|v| @fval = v}
        while @fval == 0 do; end
        @fval.should == 2**5
      end
    end
    
  end
end