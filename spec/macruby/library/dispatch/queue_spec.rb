require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  describe "Dispatch::Queue" do
    before :each do
      @q = Dispatch::Queue.new('org.macruby.gcd_spec.prelude')
    end
    
    describe "stride" do
      it "accepts a count, stride and block and yields it that many times, with an index" do
        @i = 0
        @q.stride(10) { |j| @i += j; p "outer #{j}" }
        @i.should == 45
        @i = 0
        @q.stride(10, 3) { |j| @i += j }
        @i.should == 45
        @i = 0
        @q.stride(12, 3) { |j| @i += j }
        @i.should == 66
        @i = 42
        @q.stride(0, 1) { |j| @i += 1 }
        @i.should == 42
      end
    end
  end
end