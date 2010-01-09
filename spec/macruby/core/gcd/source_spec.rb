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

      it "NOT for timer source" do
        Dispatch::Source.const_defined?(:TIMER).should == false
      end

      it "NOT for mach sources" do
        Dispatch::Source.const_defined?(:MACH_SEND).should == false
        Dispatch::Source.const_defined?(:MACH_RECV).should == false
      end
    end

    describe "of type" do
      before :each do
        @q = Dispatch::Queue.new('org.macruby.gcd_spec.sources')
      end

      describe :DATA_ADD do
        before :each do
          @type = Dispatch::Source::DATA_ADD
        end

        it "returns an instance of Dispatch::Source" do
          src = Dispatch::Source.new(@type, 0, 0, @q) { true.should == true }
          src.should be_kind_of(Dispatch::Source)
        end
      end

      describe :DATA_OR do
        before :each do
          @type = Dispatch::Source::DATA_OR
        end

        it "returns an instance of Dispatch::Source" do
          src = Dispatch::Source.new(@type, 0, 0, @q) { true.should == true }
          src.should be_kind_of(Dispatch::Source)
        end
      end

    end
    
  end
end
