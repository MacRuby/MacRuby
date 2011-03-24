require File.dirname(__FILE__) + "/../../spec_helper"

if MACOSX_VERSION >= 10.6  
  describe "Dispatch::Group" do
    before :each do
      @q = Dispatch::Queue.new('org.macruby.gcd_spec.group')
      @g = Dispatch::Group.new
      @i = 0
    end
    
    it "returns an instance of Group" do
      @g.should be_kind_of(Dispatch::Group)
    end
    
    it "is passed as an argument to Dispatch::Queue#async" do
      lambda { @q.async(@g) { @i = 42 } }.should_not raise_error(ArgumentError)
      lambda { @q.async(42) }.should raise_error(ArgumentError)
    end

    describe :wait do

      it "can wait forever until associated blocks have been run" do
        @q.async(@g) { @i = 42 }
        @g.wait(Dispatch::TIME_FOREVER)
        @i.should == 42
      end

      it "can only wait until timeout for associated blocks to run" do
        @i.should == 0
        @q.async(@g) { sleep 5; @i = 42 }
        @g.wait(0.1)
        @i.should == 0
      end

      it "will wait forever if no timeout is specified" do
        @q.async(@g) {sleep 0.2;  @i = 42 }
        @g.wait
        @i.should == 42
      end

      it "will return false if it times out" do
        @q.async(@g) {sleep 1}
        @g.wait(0.2).should == false
      end

      it "will return true if it succeeds" do
        @g.wait(Dispatch::TIME_FOREVER).should == true
      end

    end 

    describe :notify, :shared => true do
      it "will run a notify block on the specified queue" do
        @g.notify(@q) { @i += 58 }
        @q.sync {}
        @i.should == 58
      end

      it "will run the notify block only after all associated blocks have been run" do
        q2 = Dispatch::Queue.new('org.macruby.gcd_spec.group2')
        @q.suspend!
        @q.async(@g) { @i = 42 }
        @i.should == 0
        @g.notify(q2) { @i += 58 }
        @i.should == 0
        q2.suspend!
        @q.resume!
        @q.sync {}
        @i.should == 42
        q2.resume!
        q2.sync {}
        @i.should == 100
      end
    end
    
  end
  
 end
