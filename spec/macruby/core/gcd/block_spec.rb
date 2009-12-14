require File.dirname(__FILE__) + "/../../spec_helper"

if MACOSX_VERSION >= 10.6  

  describe "Dispatch blocks" do
    before :each do
      @q = Dispatch::Queue.new('org.macruby.gcd_spec.blocks')
    end

    it "should create const copies of dynamic (local) variables" do
      i = 42
      @q.sync {i = 1}
      i.should == 42
    end

    it "should have read-write references to instance (__block) variables" do
      @i = 42
      @q.sync {@i = 1}
      @i.should == 1
    end
  end

end
