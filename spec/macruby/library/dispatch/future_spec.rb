require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  describe "Dispatch::Future" do
    
    before :each do
      @future = Dispatch::Future.new { Math.sqrt(2**10) }
    end

    describe :new do
      it "should return an Future for tracking execution of the passed block" do
        @future.should be_kind_of Dispatch::Future
      end
    end
    
    describe :call do
      it "should wait Synchronously to return value" do
        @future.call.should == 2**5
      end

      it "should invoke passed block Asynchronously with returned value " do
        $global = 0
        @future.call {|v| $global = v}
        while $global == 0 do; end
        $global.should == 2**5
      end
    end
  end
end