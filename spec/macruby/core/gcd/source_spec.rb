require File.dirname(__FILE__) + "/../../spec_helper"

if MACOSX_VERSION >= 10.6

  describe "Dispatch::Source" do

    describe "constants" do
      it "for custom sources" do
        Dispatch::Source.const_defined?(:DATA_ADD).should == true
        Dispatch::Source.const_defined?(:DATA_OR).should == true
      end

      it "for process sources" do
        Dispatch::Source.const_defined?(:PROC).should == true
        Dispatch::Source.const_defined?(:SIGNAL).should == true
      end

      it "for file sources" do
        Dispatch::Source.const_defined?(:READ).should == true
        Dispatch::Source.const_defined?(:VNODE).should == true
        Dispatch::Source.const_defined?(:WRITE).should == true
      end

      it "for timer source" do
        Dispatch::Source.const_defined?(:TIME).should == true
      end

      it "NOT for mach sources" do
        Dispatch::Source.const_defined?(:MACH_SEND).should == false
        Dispatch::Source.const_defined?(:MACH_RECV).should == false
      end
    end
    
    describe "event handler" do
      before :each do
        @q = Dispatch::Queue.concurrent
        @src = Dispatch::Source.new() #@type, @handle, mask, @q
      end

      it "can be set" do
        true.should == false
      end

      it "will be invoked" do
        true.should == false
      end

      it "receives the source" do
        true.should == false
      end

      it "can get data" do
        true.should == false
      end

      it "will get merged data" do
        true.should == false
      end

      it "can get handle" do
        true.should == false
      end

      it "can get mask" do
        true.should == false
      end

    end

    describe "cancel handler" do
      before :each do
        @q = Dispatch::Queue.concurrent
        @src = Dispatch::Source.new() #@type, @handle, mask, @q
      end

      it "can be set" do
        true.should == false
      end

      it "will be invoked" do
        true.should == false
      end
    end


  end

end
