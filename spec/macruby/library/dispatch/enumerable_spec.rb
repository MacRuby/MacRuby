require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  describe "parallel loop" do
    
    describe :Integer do
      describe :p_times do
        before :each do
          @count = 4
          @ary = Array.new
          @p_ary = Dispatch::Proxy.new []
        end

        it "runs the block that many times" do
          @count.times { |j| @ary << 1 }
          @count.p_times { |j| @p_ary << 1 }
          @p_ary.size.should == @ary.size
        end

        it "runs the block passing the current index" do
          @count.times { |j| @ary << j }
          @count.p_times { |j| @p_ary << j}
          @p_ary.sort.should == @ary
        end

        it "does not run the block if the count is zero" do
          0.p_times { |j| @ary << 1 }
          @ary.size.should == 0
        end

        it "properly combines blocks with even stride > 1" do
          @count.times { |j| @ary << j }
          @count.p_times(2) { |j| @p_ary << j}
          @p_ary.sort.should == @ary
        end

        it "properly combines blocks with uneven stride" do
          @count.times { |j| @ary << j }
          @count.p_times(3) { |j| @p_ary << j}
          @p_ary.sort.should == @ary
        end

        it "properly rounds stride fraction of 0.5" do
          6.times { |j| @ary << j }
          6.p_times(4) { |j| @p_ary << j}
          @p_ary.sort.should == @ary
        end

        it "properly rounds stride fraction > 0.5" do
          7.times { |j| @ary << j }
          7.p_times(4) { |j| @p_ary << j}
          @p_ary.sort.should == @ary
        end
      end
    end

    describe "Enumerable" do
      before :each do
        @ary = (1..3).to_a
      end

      describe :p_each do
        it "exists on objects that support Enumerable" do
          @ary.respond_to?(:p_each).should == true
        end

        it "should behave like each" do
          @ary1 = 0
          @ary.each {|v| @ary1 << v*v}
          @ary2 = 0
          @q = Dispatch::Queue.for(@ary2)
          @ary.p_each {|v| temp = v*v; @q.sync {@ary2 << temp} }
          @ary2.should == @ary1
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
          @ary1 = 0
          @ary.each_with_index {|v, i| @ary1 << v**i}
          @ary2 = 0
          @q = Dispatch::Queue.for(@ary2)
          @ary.p_each_with_index {|v, i| temp = v**i; @q.sync {@ary2 << temp} }
          @ary2.should == @ary1
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
end