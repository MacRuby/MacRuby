require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6

  describe "Dispatch::Queue source from" do
    before :each do
      @q = Dispatch::Queue.new('org.macruby.gcd_spec.prelude')
      @src = nil
    end

    after :each do
      @src.cancel! if not @src.nil? and not @src.cancelled?
      @q.sync { }
    end

    describe "on_add" do
      it "fires with data on summed inputs" do
        @count = 0
        @src = Dispatch::Source.on_add(@q) {|s| @count += s.data}
        @src << 20
        @src << 22
        @q.sync {}
        @count.should == 42
      end
    end    

    describe "on_or" do
      it "fires with data on ORed inputs" do
        @count = 0
        @src = Dispatch::Source.on_or(@q) {|s| @count += s.data}
        @src << 0b101_000
        @src << 0b000_010
        @q.sync {}
        @count.should == 42
      end
    end    

    describe "on_process_event" do
      it "fires with data indicating which process event(s)" do
        @signal = Signal.list["USR1"]
        @event = nil
        @src = Dispatch::Source.on_process_event($$, %w(exit fork exec signal), @q) do 
           |s| @event = s.data
        end
        Signal.trap(@signal, "IGNORE")
        Process.kill(@signal, $$)
        Signal.trap(@signal, "DEFAULT")
        @q.sync {}
        (@event & Dispatch::Source.proc_event(:signal)).should > 0
      end
    end

    describe "on_signal" do
      it "fires with data on how often the process was signaled" do
        @signal = Signal.list["USR1"]
        @count = 0
        @src = Dispatch::Source.on_signal(@signal, @q) {|s| @count += s.data}
        Signal.trap(@signal, "IGNORE")
        Process.kill(@signal, $$)
        Process.kill(@signal, $$)
        Signal.trap(@signal, "DEFAULT")
        @q.sync {}
        @count.should == 2
        @src.cancel!
      end
    end    

    describe "file" do
      before :each do
        @msg = "#{$$}-#{Time.now}"
        @filename = "/var/tmp/gcd_spec_source-#{@msg}"
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
          @src = Dispatch::Source.on_read(@file, @q) {|s| @result<<@file.read(s.data)}
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
          @src = Dispatch::Source.on_read(@file, @q) do |s|
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
        it "fires with data indicating which file event(s)" do
          @file = File.open(@filename, "w")
          @fired = false
          events = %w(delete write extend attrib link rename revoke)
          @src = Dispatch::Source.on_file_event(@file, events, @q) do |s|
              @flag = s.data
              @fired = true
          end
          @file.write(@msg)
          @file.flush
          @q.sync { }
          #while (@fired == false) do; end
          @fired.should == true
          @flag.should == Dispatch::Source.vnode_event(:write)
        end
      end          
    end

    describe "on_interval" do
      it "fires with data on how often the timer has fired" do
        @count = -1
        repeats = 2
        @interval = 0.02
        @src = Dispatch::Source.on_interval(@interval, @q) {|s| @count += s.data}
        sleep repeats*@interval
        @q.sync { }
        @count.should == repeats
      end
    end
  end
  
end
