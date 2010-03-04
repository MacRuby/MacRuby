require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6

  describe "Dispatch::Source" do
    before :each do
      @q = Dispatch::Queue.new('org.macruby.gcd_spec.prelude')
      @src = nil
    end

    after :each do
      @src.cancel! if not @src.nil? and not @src.cancelled?
      @q.sync { }
    end


    describe :event2num do
      it "converts PROC symbol to int" do
        Dispatch::Source.event2num(:signal).should == Dispatch::Source::PROC_SIGNAL
      end

      it "converts VNODE symbol to int" do
        Dispatch::Source.event2num(:rename).should == Dispatch::Source::VNODE_RENAME
      end
    end
    
    describe :data2events do
      it "converts PROC bitfields to symbols" do
        mask = Dispatch::Source::PROC_EXIT | Dispatch::Source::PROC_SIGNAL 
        events = Dispatch::Source.data2events(mask)
        events.include?(:signal).should == true
        events.include?(:fork).should == false
      end

      it "converts VNODE bitfields to symbols" do
        mask = Dispatch::Source::VNODE_DELETE | Dispatch::Source::VNODE_WRITE
        events = Dispatch::Source.data2events(mask)
        events.include?(:delete).should == true
        events.include?(:rename).should == false
      end
    end
    
    describe "add" do
      it "fires with data on summed inputs" do
        @count = 0
        @src = Dispatch::Source.add(@q) {|s| @count += s.data}
        @src << 20
        @src << 22
        @q.sync {}
        @count.should == 42
      end
    end    

    describe "or" do
      it "fires with data on ORed inputs" do
        @count = 0
        @src = Dispatch::Source.or(@q) {|s| @count += s.data}
        @src << 0b101_000
        @src << 0b000_010
        @q.sync {}
        @count.should == 42
      end
    end    

    describe "PROC" do
      before :each do
        @signal = Signal.list["USR1"]
      end

      describe "process" do
        
        it "fires with data on process event(s)" do
          @event = 0
          @events = []
          @src = Dispatch::Source.process($$, %w(exit fork exec signal), @q) do |s|
             @event += s.data
             @events += Dispatch::Source.data2events(s.data)
          end
          Signal.trap(@signal, "IGNORE")
          Process.kill(@signal, $$)
          Signal.trap(@signal, "DEFAULT")
          @q.sync {}
          @events.include?(:signal).should == true
        end
      
        it "can use bitfields as well as arrays" do
          mask = Dispatch::Source::PROC_EXIT | Dispatch::Source::PROC_SIGNAL 
          @event = 0
          @src = Dispatch::Source.process($$, mask, @q) { |s| @event |= s.data }
          Signal.trap(@signal, "IGNORE")
          Process.kill(@signal, $$)
          Signal.trap(@signal, "DEFAULT")
          @q.sync {}
          @event.should == Dispatch::Source.event2num(:signal)
        end
      end

      describe "signal" do
        it "fires with data on signal count" do
          @count = 0
          @src = Dispatch::Source.signal(@signal, @q) {|s| @count += s.data}
          Signal.trap(@signal, "IGNORE")
          Process.kill(@signal, $$)
          Process.kill(@signal, $$)
          Signal.trap(@signal, "DEFAULT")
          @q.sync {}
          @count.should == 2
          @src.cancel!
        end
      end  
    end  

    describe "VNODE" do
      before :each do
        @msg = "#{$$}-#{Time.now}"
        @filename = tmp("gcd_spec_source-#{@msg}")
        @file = nil
        @src = nil
      end

      after :each do
        @src.cancel! if not @src.nil? and not @src.cancelled?
        @q.sync { }
        @file.close if not @file.closed?
        File.delete(@filename)
      end

      describe "read" do
        it "fires with data on readable bytes" do
          File.open(@filename, "w") {|f| f.print @msg}
          @file = File.open(@filename, "r")
          @result = ""
          @src = Dispatch::Source.read(@file, @q) {|s| @result<<@file.read(s.data)}
          while (@result.size < @msg.size) do; end
          @q.sync { }
          @result.should == @msg
        end
      end    

      describe "write" do
        it "fires with data on writable bytes" do
          @file = File.open(@filename, "w")
          @pos = 0
          @message = @msg
          @src = Dispatch::Source.read(@file, @q) do |s|
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
      
      describe "file" do
        it "fires with data on file events" do
          write_flag = Dispatch::Source.event2num(:write)
          @file = File.open(@filename, "w")
          @fired = false
          @mask = 0
          events = %w(delete write extend attrib link rename revoke)
          @src = Dispatch::Source.file(@file, events, @q) do |s|
              @mask |= s.data
              @fired = true
          end
          @file.write(@msg)
          @file.flush
          @q.sync { }
          @fired.should == true
          (@mask & write_flag).should == write_flag
        end
      end          
    end

    describe "periodic" do
      it "fires with data on how often the timer has fired" do
        @count = -1
        repeats = 2
        @periodic = 0.02
        @src = Dispatch::Source.periodic(@periodic, @q) {|s| @count += s.data}
        sleep repeats*@periodic
        @q.sync { }
        @count.should == repeats
      end
    end
  end
  
end
