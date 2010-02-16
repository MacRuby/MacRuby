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

      it "should behave like each" do
        @sum1 = 0
        @ary.each {|v| @sum1 += v*v}
        @sum2 = 0
        @q = Dispatch.queue(@sum2)
        @ary.p_each {|v| temp = v*v; @q.sync {@sum2 += temp} }
        @sum2.should == @sum1
      end
      
      it "should execute concurrently" do
        t0 = Time.now
        @ary.p_each {|v| sleep v/100.0}
        t1 = Time.now
        t_total = @ary.inject(0) {|a,b| a + b/100.0}
        (t1-t0).to_f.should < t_total
      end
    end
    
    describe :p_each_with_index do
      it "exists on objects that support Enumerable" do
        @ary.respond_to?(:p_each).should == true
      end
      
      it "should behave like each_with_index" do
        @sum1 = 0
        @ary.each_with_index {|v, i| @sum1 += v**i}
        @sum2 = 0
        @q = Dispatch.queue(@sum2)
        @ary.p_each_with_index {|v, i| temp = v**i; @q.sync {@sum2 += temp} }
        @sum2.should == @sum1
      end
    end
    
    describe :p_map do
      it "exists on objects that support Enumerable" do
        @ary.respond_to?(:p_map).should == true
      end
      
      it "should behave like map" do
        map1 = @ary.map {|v| v*v}
        map2 = @ary.p_map {|v| v*v}
        map2.should == map1
      end
    end

    describe :p_mapreduce do
      it "exists on objects that support Enumerable" do
        @ary.respond_to?(:p_mapreduce).should == true
      end
      
      it "should behave like an unordered map" do
        map1 = @ary.map {|v| v*v}
        map2 = @ary.p_mapreduce([]) {|v| [v*v]}
        map2.sort.should == map1
      end

      it "should accumulate any object that takes :<< " do
        map1 = @ary.map {|v| "%x" % (10+v)}
        map2 = @ary.p_mapreduce("") {|v| "%x" % (10+v)}   
        map1.each do |s|
          map2.index(s).should_not == nil
        end
      end

      it "should allow custom accumulator methods" do
        map1 = @ary.map {|v| v**2}
        sum1 = map1.inject(0) {|s,v| s | v}
        sum2 = @ary.p_mapreduce(0, :|) {|v| v**2}   
        sum2.should == sum1
      end
    end

    describe :p_find_all do
      it "exists on objects that support Enumerable" do
        @ary.respond_to?(:p_find_all).should == true
      end  

      it "should behave like find_all" do
        found1 = @ary.find_all {|v| v.odd?}
        found2 = @ary.p_find_all {|v| v.odd?}
        found2.sort.should == found1
      end
    end

    describe :p_find do
      it "exists on objects that support Enumerable" do
        @ary.respond_to?(:p_find).should == true
      end  

      it "returns nil if nothing found" do
        found2 = @ary.p_find {|v| false}
        found2.should.nil?
      end

      it "returns one element that matches the condition" do
        found1 = @ary.find_all {|v| v.odd?}
        found2 = @ary.p_find {|v| v.odd?}
        found2.should_not.nil?
        found1.include? found2
      end
    end
    
  end
end