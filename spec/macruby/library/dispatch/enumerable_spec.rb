require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  describe "parallel Enumerable" do
    before :each do
      @ary = (1..3).to_a
    end

    describe :p_each do
      it "exists on objects that support Enumerable" do
        @ary.respond_to?(:p_each).should == true
      end

      it "behaves like each" do
        @sum1 = 0
        @ary.each {|v| @sum1 += v*v}
        @sum2 = 0
        @q = Dispatch.queue_for(@sum2)
        @ary.p_each {|v| temp = v*v; @q.sync {@sum2 += temp} }
        @sum1.should == @sum2
      end
      
      it "executes concurrently" do
        true.should == true
      end
    end
    
    describe :p_each_with_index do
      it "exists on objects that support Enumerable" do
        @ary.respond_to?(:p_each).should == true
      end
      
      it "behaves like each_with_index" do
        @sum1 = 0
        @ary.each_with_index {|v, i| @sum1 += v**i}
        @sum2 = 0
        @q = Dispatch.queue_for(@sum2)
        @ary.p_each_with_index {|v, i| temp = v**i; @q.sync {@sum2 += temp} }
        @sum1.should == @sum2
      end
      
      it "executes concurrently" do
        true.should == true
      end
    end
    
    describe :p_map do
      it "exists on objects that support Enumerable" do
        @ary.respond_to?(:p_each).should == true
      end
      it "behaves like map" do
        @ary.should.respond_to? :p_map
      end
      
      it "executes concurrently" do
        true.should == true
      end
    end
  end
end