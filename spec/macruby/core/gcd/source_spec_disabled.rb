require File.dirname(__FILE__) + "/../../spec_helper"

if MACOSX_VERSION >= 10.6

  describe "Dispatch::Source" do
    before :each do
      @q = Dispatch::Queue.new('org.macruby.gcd_spec.sources')
    end

    describe "custom events" do
      describe "on_add" do
        before :each do
          @src = @q.on_add {|count| count.should == 42}
        end

        it "returns a Source for a queue " do
          @src.should be_kind_of(Dispatch::Source)
        end

        it "should not be suspended" do
          @src.suspended?.should == false
        end

        it "takes an event handler block" do
          lambda { @q.on_add }.should raise_error(ArgumentError)
        end

        it "takes no arguments" do
          lambda { @q.on_add(1) { }}.should raise_error(ArgumentError)
        end

        it "should execute event handler when Fixnum merged with <<" do
          @src.suspend
          @src << 20
          @src << 22
          @src.resume
          @q.sync {}
        end

        it "will only merge Fixnum arguments" do
          lambda { @src << :foo}.should raise_error(ArgumentError)
        end
      end    

      describe "on_or" do
        before :each do
          @src = @q.on_or {|count| count.should == 0b1011}
        end

        it "returns a Source for a queue " do
          @src.should be_kind_of(Dispatch::Source)
        end

        it "should not be suspended" do
          @src.suspended?.should == false
        end

        it "takes an event handler block" do
          lambda { @q.on_or }.should raise_error(ArgumentError)
        end

        it "takes no arguments" do
          lambda { @q.on_or(1) { }}.should raise_error(ArgumentError)
        end

        it "fires the event handler when merging data using <<" do
          @src.suspend
          @src << 0b0011
          @src << 0b1010
          @src.resume
          @q.sync {}
        end

        it "will only merge Fixnum arguments" do
          lambda { @src << :foo}.should raise_error(ArgumentError)
        end
      end    
    end

    describe "process events" do
      before :each do
        @test_signal = "USR1"
      end

      describe "on_process_event" do
        before :each do
          @src = @q.on_process_event($$, :exit, :fork, :exec, :reap, :signal) { |mask| mask[0].should == :signal }
        end

        after :each do
          @src.cancel
        end

        it "returns a Source for a queue " do
          @src.should be_kind_of(Dispatch::Source)
        end

        it "should not be suspended" do
          @src.suspended?.should == false
        end

        it "takes an event handler block" do
          lambda { @q.on_process_event($$, :exit, :fork, :exec, :reap, :signal) }.should raise_error(ArgumentError)
        end

        it "takes a process_id plus one or more symbols" do
          lambda { @q.on_process_event(:exit, :fork, :exec, :reap, :signal) {} }.should raise_error(ArgumentError)
          lambda { @q.on_process_event($$) {} }.should raise_error(ArgumentError)
        end

        it "should automatically mask any handled signals (?)" do
          @q.on_signal(:KILL) {}
          #@Process.kill(:KILL, $$) Don't try this very often until it works :-)
          @q.sync {}

        end

        it "fires the event handler with a mask indicating the process event" do
          Process.kill(@test_signal, $$)                # send myself a SIGUSR1
          @q.sync {}
        end
      end

      describe "on_signal" do
        before :each do
          @src = @q.on_signal(@test_signal) { |count| count == 2 }
        end

        after :each do
          @src.cancel
        end

        it "returns a Source for a queue " do
          @src.should be_kind_of(Dispatch::Source)
        end

        it "should not be suspended" do
          @src.suspended?.should == false
        end

        it "takes an event handler block" do
          lambda { @q.on_signal(@test_signal) }.should raise_error(ArgumentError)
        end

        it "takes the same signal identifiers as Process#kill" do
          lambda { @q.on_signal(9) {} }.should_not raise_error(ArgumentError)
          lambda { @q.on_signal("KILL") {} }.should_not raise_error(ArgumentError)
          lambda { @q.on_signal("SIG_KILL") {} }.should_not raise_error(ArgumentError)
          lambda { @q.on_signal(:KILL) {} }.should_not raise_error(ArgumentError)
          lambda { @q.on_signal(9.5) {} }.should raise_error(ArgumentError)
        end

        it "fires the event handler with count of how often the current process is signaled" do
          @src.suspend
          Process.kill(@test_signal, $$)                # send myself a SIGUSR1
          Process.kill(@test_signal, $$)                # send myself a SIGUSR1
          @src.resume
          @q.sync {}
        end

      end    

    end    

    describe "on_timer" do
      before :each do
        @src = @q.on_timer(0, 0.1, 0.01) { |count| count.should >= 2 }
      end

      after :each do
        @src.cancel
      end

      it "returns a Source for a queue " do
        @src.should be_kind_of(Dispatch::Source)
      end

      it "should not be suspended" do
        @src.suspended?.should == false
      end

      it "takes an event handler block" do
        lambda { @q.on_timer(0, 0.1, 0.01) }.should raise_error(ArgumentError)
      end

      it "takes delay, interval in seconds, and optional leeway" do
        lambda { @q.on_timer(0, 0.1, 0.01) {} }.should_not raise_error(ArgumentError)
        lambda { @q.on_timer(0, 0.1) {} }.should_not raise_error(ArgumentError)
        lambda { @q.on_timer(0.1, Time.now) {} }.should raise_error(TypeError)
      end

      it "fires the event handler with count of how often the timer has fired" do
        sleep 0.3
        @q.sync {}
      end

    end    

    describe "file events" do
      before :each do
        @filename = tmp("gcd_spec_source-#{$$}")
          @msg = "#{Time.now}: on_file_event"
      end

      describe "on_file_event" do
        before :each do
          @file = File.open(@filename, "w")
          @src = @q.on_file_event(@file, :delete, :write, :extend, :attrib, :link, :rename, :revoke) {|mask| mask[0].should == :write}
        end

        after :each do
          @src.cancel
          @file.close
        end

        it "returns a Source for a queue " do
          @src.should be_kind_of(Dispatch::Source)
        end

        it "should not be suspended" do
          @src.suspended?.should == false
        end

        it "takes an event handler block" do
          lambda { @q.on_file_event(file, :delete) }.should raise_error(ArgumentError)
        end

        it "takes an IO object plus one or more symbols" do
          lambda { @q.on_file_event(file, :delete) {}}.should_not raise_error(ArgumentError)
          lambda { @q.on_file_event(file) {} }.should raise_error(ArgumentError)
          lambda { @q.on_file_event(:delete) {} }.should raise_error(ArgumentError)
        end

        it "fires the event handler with a mask indicating the file event" do
          @file.puts @msg
          @q.sync {}
        end
      end    

      describe "on_write" do
        before :each do
          @file = File.open(@filename, "w")
          @src = @q.on_write(@file) { |count| @file.puts @msg if @msg.size < count}
        end

        after :each do
          @file.close
        end

        it "returns a Source for a queue " do
          @src.should be_kind_of(Dispatch::Source)
        end

        it "should not be suspended" do
          @src.suspended?.should == false
        end

        it "takes an event handler block" do
          lambda { @q.on_write(@file) }.should raise_error(ArgumentError)
        end

        it "takes an IO object" do
          lambda { @q.on_write(@file) {}}.should_not raise_error(ArgumentError)
          lambda { @q.on_write(@filename) {}}.should raise_error(ArgumentError)
        end

        it "fires the event handler with the number of bytes that can be written" do
          @q.sync {}
        end

        it "should ensure the file is closed when the source is cancelled (?)" do
        end
      end    

 
      describe "on_read" do
        before :each do
          @file = File.open(@filename, "r")
          @src = @q.on_read(@file) { |count| d = file.gets if count > @msg.size }
        end

        after :each do
          @file.close
        end

        it "returns a Source for a queue " do
          @src.should be_kind_of(Dispatch::Source)
        end

        it "should not be suspended" do
          @src.suspended?.should == false
        end

        it "takes an event handler block" do
          lambda { @q.on_read(@file) }.should raise_error(ArgumentError)
        end

        it "takes an IO object" do
          lambda { @q.on_read(@file) {}}.should_not raise_error(ArgumentError)
          lambda { @q.on_read(@filename) {}}.should raise_error(ArgumentError)
        end

        it "fires the event handler with the number of bytes that can be read" do
          @q.sync {}
        end

        it "should ensure the file is closed when the source is cancelled (?)" do
        end

      end    
   end

  end

end
