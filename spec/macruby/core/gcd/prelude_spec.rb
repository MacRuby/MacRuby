require File.dirname(__FILE__) + "/../../spec_helper"
require File.dirname(__FILE__) + "/../../../../gcd_prelude"

if MACOSX_VERSION >= 10.6

  describe "Dispatch::Queue convenience method:" do
    before :each do
      @q = Dispatch::Queue.new('org.macruby.gcd_spec.sources')
      @src = nil
    end

    after :each do
      @src.cancel! if not @src.nil? and not @src.cancelled?
      @q.sync { }
    end

    describe "on_add" do
      it "fires with data on summed inputs" do
        @count = 0
        @src = @q.on_add {|s| @count += s.data}
        @src.suspend!
        @src << 20
        @src << 22
        @src.resume!
        @q.sync {}
        @count.should == 42
      end
    end    

    describe "on_or" do
      it "fires with data on ORed inputs" do
        @count = 0
        @src = @q.on_or {|s| @count += s.data}
        @src.suspend!
        @src << 0xb101_000
        @src << 0xb000_010
        @src.resume!
        @q.sync {}
        @count.should == 42
      end
    end    

    describe "on_process_event" do
      it "fires with data indicating which process event(s)" do
        @signal = Signal.list["USR1"]
        @event = nil
        @src = @q.on_process_event($$, :exit,:fork,:exec,:reap,:signal) do 
           |s| @event = s.data
        end
        Process.kill(@signal, $$)
        @q.sync {}
        @event.should == Dispatch::Source::PROC_SIGNAL
      end
    end

    describe "on_signal" do
      it "fires with data on how often the process was signaled" do
        @signal = Signal.list["USR1"]
        @count = 0
        @src = @q.on_signal(@signal) {|s| @count += s.data}
        Signal.trap(@signal, "IGNORE")
        Process.kill(@signal, $$)
        Process.kill(@signal, $$)
        Signal.trap(@signal, "DEFAULT")
        @q.sync {}
        @count.should == 2
        @src.cancel!
      end
    end    

    describe "file:" do
      before :each do
        @msg = "#{$$}: #{Time.now}"
        @filename = "/var/tmp/gcd_spec_source-#{$$}-#{Time.now}"
        @file = nil
        @src = nil
      end

      after :each do
        @src.cancel! if not @src.nil? and not @src.cancelled?
        @q.sync { }
        @file.close if not @file.closed?
        File.delete(@filename)
      end

      describe "on_read" do
        it "fires with data on how many bytes can be read" do
          File.open(@filename, "w") {|f| f.print @msg}
          @file = File.open(@filename, "r")
          @result = ""
          @src = @q.on_read(@file, close:true) {|s| @result<<@file.read(s.data)}
          while (@result.size < @msg.size) do; end
          @q.sync { }
          @result.should == @msg
        end
      end    

      describe "on_write" do
        it "fires with data on how many bytes can be written" do
          @file = File.open(@filename, "w")
          @pos = 0
          @message = @msg
          @src = @q.on_read(@file, close:true) do |s|
            pos = s.data
            if not @message.nil? then
              next_msg = @message[0..pos-1]
              @file.write(next_msg)
              @message = @message[pos..-1]
            end
          end
          while (@result.size < @msg.size) do; end
          @q.sync { }
          @result.should == @msg
        end
      end
      
      describe "on_file_event" do
        it "fires with data on how many bytes can be written" do
          @file = File.open(@filename, "w")
          @fired = false
          @src = @q.on_file_event(@file, :delete, :write, :extend, :attrib, :link, :rename, :revoke) do |s|
              @flag = s.data
              @fired = true
          end
          @file.write(@msg)
          @file.flush
          @q.sync { }
          #while (@fired == false) do; end
          @fired.should == true
          @flag.should == Dispatch::Source::VNODE_WRITE
        end
      end          

      describe "on_interval" do
        it "fires with data on how often the timer has fired" do
          @count = -1
          repeats = 2
          @interval = 0.02
          @src = @q.on_interval(@interval) {|s| @count += s.data}
          sleep repeats*@interval
          @q.sync { }
          @count.should == repeats
        end
      end
    end
  end
  
end
