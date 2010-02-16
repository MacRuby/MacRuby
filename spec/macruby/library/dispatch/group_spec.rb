require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  describe "Dispatch::Group" do
    
    before :each do
      @q = Dispatch::Queue.new('org.macruby.gcd_spec.group')
      @g = Dispatch::Group.new
      @i = 0
    end
    
    describe :join do
      it "should wait until execution is complete if no block is passed" do
        @q.async(@g) { @i = 42 }
        @g.join
        @i.should == 42
      end

      it "should run a notify block on completion, if passed" do
        @q.async(@g) { @i = 42 }
        @g.join {@i *= 2}
        while @i <= 42 do; end    
        @i.should == 84
      end

      it "should run notify block on specified queue, if any" do
        @q.async(@g) { @i = 42 }
        @q.sync { }
        @g.join(@q) {@i *= 2}
        @q.sync { }
        @i.should == 84
      end
    end

  end
end
