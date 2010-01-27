require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  describe "Dispatch::Queue" do
    before :each do
      @q = Dispatch::Queue.new('org.macruby.gcd_spec.prelude')
      @count = 4
      @sum = 0
    end
    
    describe "stride" do
      it "expects a count, stride and block " do
        lambda { @q.stride(@count) { |j| @sum += 1 } }.should raise_error(ArgumentError)
        lambda { @q.stride(@count, 1) }.should raise_error(NoMethodError)
      end

      it "runs the block +count+ number of times" do
        @q.stride(@count, 1) { |j| @sum += 1 }
        @sum.should == @count
      end

      it "runs the block passing the current index" do
        @q.stride(@count, 1) { |j| @sum += j }
        @sum.should == (@count*(@count-1)/2)
      end

      it "does not run the block if the count is zero" do
        @q.stride(0, 1) { |j| @sum += 1 }
        @sum.should == 0
      end
      
      it "properly combines blocks with even stride > 1" do
        @q.stride(@count, 2) { |j| @sum += j }
        @sum.should == (@count*(@count-1)/2)
      end

      it "properly combines blocks with uneven stride > 1" do
        @q.stride(5, 2) { |j| @sum += j }
        @sum.should == (5*(5-1)/2)
      end

      it "properly rounds stride fractions > 0.5" do
        @q.stride(7, 4) { |j| @sum += j }
        @sum.should == (7*(7-1)/2)
      end
    end
  end
end