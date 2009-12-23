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

    describe "new" do
      before :each do
        @q = Dispatch::Queue.concurrent
      end

      it "can create a custom Source" do
        Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0,  0, @q) {}.should
          be_kind_of(Dispatch::Source)
        Dispatch::Source.new(Dispatch::Source::DATA_OR, 0,  0, @q) {}.should
          be_kind_of(Dispatch::Source)
      end    

      it "can create a process Source" do
        Dispatch::Source.new(Dispatch::Source::PROC, 0,  0, @q) {}.should
          be_kind_of(Dispatch::Source)
        Dispatch::Source.new(Dispatch::Source::SIGNAL, 0,  0, @q) {}.should
          be_kind_of(Dispatch::Source)
      end    

      it "can create a file Source" do
        Dispatch::Source.new(Dispatch::Source::READ, 0,  0, @q) {}.should
          be_kind_of(Dispatch::Source)
        Dispatch::Source.new(Dispatch::Source::VNODE, 0,  0, @q) {}.should
          be_kind_of(Dispatch::Source)
        Dispatch::Source.new(Dispatch::Source::WRITE, 0,  0, @q) {}.should
          be_kind_of(Dispatch::Source)
      end    

      it "can create a Timer " do
        Dispatch::Timer.new(@q, nil, 0) {}.should
          be_kind_of(Dispatch::Source)
      end    
      
      it "raises an ArgumentError if no block is given" do
        lambda { Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0,  0, @q)
                }.should raise_error(ArgumentError) 
      end
      
    end
      
    describe "event handler" do

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

  end

end
