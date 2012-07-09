require File.dirname(__FILE__) + "/../../spec_helper"

if MACOSX_VERSION >= 10.6

  describe "Dispatch::Source" do

    describe "constants" do
      it "for custom source types" do
        Dispatch::Source.const_defined?(:DATA_ADD).should == true
        Dispatch::Source.const_defined?(:DATA_OR).should == true
      end

      it "for process source types" do
        Dispatch::Source.const_defined?(:PROC).should == true
        Dispatch::Source.const_defined?(:SIGNAL).should == true
      end

      it "for file source types" do
        Dispatch::Source.const_defined?(:READ).should == true
        Dispatch::Source.const_defined?(:VNODE).should == true
        Dispatch::Source.const_defined?(:WRITE).should == true
      end

      it "NOT for timer source type" do
        Dispatch::Source.const_defined?(:TIMER).should == false
      end

      it "NOT for mach source types" do
        Dispatch::Source.const_defined?(:MACH_SEND).should == false
        Dispatch::Source.const_defined?(:MACH_RECV).should == false
      end
      
      it "for process events" do
        Dispatch::Source.const_defined?(:PROC_EXIT).should == true
        Dispatch::Source.const_defined?(:PROC_FORK).should == true
        Dispatch::Source.const_defined?(:PROC_EXEC).should == true
        Dispatch::Source.const_defined?(:PROC_SIGNAL).should == true
      end

      it "for vnode events" do
        Dispatch::Source.const_defined?(:VNODE_DELETE).should == true
        Dispatch::Source.const_defined?(:VNODE_WRITE).should == true
        Dispatch::Source.const_defined?(:VNODE_EXTEND).should == true
        Dispatch::Source.const_defined?(:VNODE_ATTRIB).should == true
        Dispatch::Source.const_defined?(:VNODE_LINK).should == true
        Dispatch::Source.const_defined?(:VNODE_RENAME).should == true
        Dispatch::Source.const_defined?(:VNODE_REVOKE).should == true
      end
      
    end

    describe "of type" do
      before :each do
        @q = Dispatch::Queue.new('org.macruby.gcd_spec.sources')
        @sm = Dispatch::Semaphore.new(0)
      end

      after :each do
        @q.sync { }
      end

      describe :DATA_ADD do
        before :each do
          @type = Dispatch::Source::DATA_ADD
        end

        it "returns an instance of Dispatch::Source" do
          src = Dispatch::Source.new(@type, 0, 0, @q) { }
          src.should be_kind_of(Dispatch::Source)
        end

        it "should not be suspended" do
          src = Dispatch::Source.new(@type, 0, 0, @q) { }
          src.suspended?.should == false
        end

        it "fires event handler on merge" do
          @i = 0
          src = Dispatch::Source.new(@type, 0, 0, @q) {|s|  @i = 42; @sm.signal}
          src << 42
          @sm.wait(0.1)
          @i.should == 42
        end        

        it "passes source to event handler" do
          @flag = false
          src = Dispatch::Source.new(@type, 0, 0, @q) do |source|
            @flag = (source.is_a? Dispatch::Source)
            @sm.signal
          end
          src << 42
          @sm.wait(0.1)
          @flag.should == true
        end        

        it "passes data to source in event handler" do
          @i = 0
          src = Dispatch::Source.new(@type, 0, 0, @q) do |source|
            @i = source.data
            @sm.signal
          end
          src << 42
          @sm.wait(0.1)
          @i.should == 42
        end        

        it "coalesces data for source in event handler" do
          @i = 0
          src = Dispatch::Source.new(@type, 0, 0, @q) do |source|
            @i = source.data
            @sm.signal
          end
          src.suspend!
          src << 17
          src << 25
          src.resume!
          @sm.wait(0.1)
          @i.should == 42
        end        
      end

      describe :DATA_OR do
        before :each do
          @type = Dispatch::Source::DATA_OR
        end

        it "returns an active instance of Dispatch::Source" do
          src = Dispatch::Source.new(@type, 0, 0, @q) { }
          src.should be_kind_of(Dispatch::Source)
          src.suspended?.should == false
        end

        it "fires event handler on merge" do
          @i = 0
          src = Dispatch::Source.new(@type, 0, 0, @q) {|s|  @i = 42; @sm.signal}
          src << 42
          @sm.wait(0.1)
          @i.should == 42
        end        
        
        it "coalesces data for source in event handler" do
          @i = 0
          src = Dispatch::Source.new(@type, 0, 0, @q) do |source|
            @i = source.data
            @sm.signal
          end
          src.suspend!
          src << 0b000_010
          src << 0b101_000
          src.resume!
          @sm.wait(0.1)
          @i.should == 42 #0b101_010
          src.cancel!
        end        
      end

      describe :PROC do
        before :each do
          @type = Dispatch::Source::PROC
          @mask = Dispatch::Source::PROC_SIGNAL
          @signal = Signal.list["USR1"]
        end

        it "returns an active instance of Dispatch::Source" do
          src = Dispatch::Source.new(@type, $$, @mask, @q) { }
          src.should be_kind_of(Dispatch::Source)
          src.suspended?.should == false
          src.cancel!
        end

        it "fires on process event with event mask data" do
          @i = 0
          @fired = false
          src = Dispatch::Source.new(@type, $$, @mask, @q) do |s|
            @i = s.data
            @fired = true
            @sm.signal
          end
          Signal.trap(@signal, "IGNORE")
          Process.kill(@signal, $$)
          Signal.trap(@signal, "DEFAULT")
          @sm.wait(0.1)
          #while (@fired == false) do; end
          @fired.should == true
          @i.should == @mask
          src.cancel!
        end
      end    

      describe :SIGNAL do
        before :each do
          @type = Dispatch::Source::SIGNAL
          @signal = Signal.list["USR2"]          
        end

        it "returns an instance of Dispatch::Source" do
          src = Dispatch::Source.new(@type, @signal, 0, @q) { }
          src.should be_kind_of(Dispatch::Source)
          #src.cancel! - why does this make the subsequent test fail?
        end

        it "fires on signal with signal count data" do
          @i = 0
          @fired = false
          src = Dispatch::Source.new(@type, @signal, 0, @q) do |s|
            @i = s.data
            @fired = true
            @sm.signal
          end
          Signal.trap(@signal, "IGNORE")
          Process.kill(@signal, $$)
          Signal.trap(@signal, "DEFAULT")
          @sm.wait(0.1)
          @fired.should == true
          @i.should == 1
          src.cancel!
        end
      end
      
      describe "file:" do
        before :each do
          @msg = "#{$$}: #{Time.now}"
          @filename = tmp "gcd_spec_source-#{$$}-#{Time.now}"
          @file = nil
          @src = nil
        end

        after :each do
          @src.cancel! if not @src.nil? and not @src.cancelled?
          @q.sync { }
          @file.close if not @file.closed?
          File.delete(@filename)
        end
      
        describe :READ do
          before :each do
            @type = Dispatch::Source::READ
            File.open(@filename, "w") {|f| f.print @msg}
            @file = File.open(@filename, "r")
          end
          
          it "returns an instance of Dispatch::Source given descriptor" do
            @src = Dispatch::Source.new(@type, @file.to_i, 0, @q) { }
            @src.should be_kind_of(Dispatch::Source)
          end

          it "returns an instance of Dispatch::Source given IO" do
            @src = Dispatch::Source.new(@type, @file, 0, @q) { }
            @src.should be_kind_of(Dispatch::Source)
          end
          
          it "fires with data on estimated # of readable bytes" do
            @result = ""
            @src = Dispatch::Source.new(@type, @file.to_i, 0, @q) do |s|
              begin
                @result << @file.read(s.data) # ideally should read_nonblock
              rescue Exception => error
                puts error
              end
            end
            while (@result.size < @msg.size) do; end
            @q.sync { }
            @result.should == @msg
          end
          
          it "does not close file when cancelled given descriptor" do
            @src = Dispatch::Source.new(@type, @file.to_i, 0, @q) { }
            @src.cancel!
            @q.sync { }
            @file.closed?.should == false
          end

          it "does close file when cancelled given IO" do
            @src = Dispatch::Source.new(@type, @file, 0, @q) { }
            @file.closed?.should == false
            @src.cancel!
            @q.sync { }
            @file.closed?.should == true
          end                    
        end    

        describe :WRITE do
          before :each do
            @type = Dispatch::Source::WRITE
            @file = File.open(@filename, "w")
          end

          it "returns an instance of Dispatch::Source" do
            @src = Dispatch::Source.new(@type, @file.to_i, 0, @q) { }
            @src.should be_kind_of(Dispatch::Source)
          end
          
          it "fires with data on estimated # of writeable bytes" do
            @pos = 0
            @src = Dispatch::Source.new(@type, @file.to_i, 0, @q) do |s|
              begin
                npos = @pos + s.data - 1
                @file.write(@msg[@pos..npos]) # ideally should write_nonblock
                @pos = npos + 1
              rescue Exception => error
                puts error
              end
            end
            while (@pos < @msg.size) do; end
            @q.sync { }
            File.read(@filename).should == @msg
            @src.cancel!            
          end

          it "does not close file when cancelled given descriptor" do
            @src = Dispatch::Source.new(@type, @file.to_i, 0, @q) { }
            @src.cancel!
            @q.sync { }
            @file.closed?.should == false
          end
          
          it "does close file when cancelled given IO" do
            @src = Dispatch::Source.new(@type, @file, 0, @q) { }
            @file.closed?.should == false
            @src.cancel!
            @q.sync { }
            @file.closed?.should == true
          end
          
        end    

        describe :VNODE do
          before :each do
            @type = Dispatch::Source::VNODE
            @mask = Dispatch::Source::VNODE_WRITE
            @file = File.open(@filename, "w")
          end

          it "returns an instance of Dispatch::Source" do
            @src = Dispatch::Source.new(@type, @file.to_i, @mask, @q) { }
            @src.should be_kind_of(Dispatch::Source)
          end
          
          it "fires with data showing mask of vnode events" do
            @flag = 0
            @fired = false
            @src = Dispatch::Source.new(@type, @file.to_i, @mask, @q) do |s|
                @flag = s.data
                @fired = true
                @sm.signal
            end
            @file.write(@msg)
            @file.flush
            @sm.wait(0.1)
            @fired.should == true
            @flag.should == @mask
          end    

          it "does not close file when cancelled given descriptor" do
            @src = Dispatch::Source.new(@type, @file.to_i, 0, @q) { }
            @src.cancel!
            @sm.wait(0.1)
            @file.closed?.should == false
          end

          it "does close file when cancelled given IO" do
            @src = Dispatch::Source.new(@type, @file, 0, @q) { }
            @file.closed?.should == false
            @src.cancel!
            @q.sync { }
            @file.closed?.should == true
          end   
        end      
      end
    end # file
    
    
    describe :Timer do
      before :each do
        @interval = 0.02
        @src = nil
      end

      after :each do
        @src.cancel!
        @q.sync { }
      end

      it "returns an instance of Dispatch::Source" do
        @src = Dispatch::Source.timer(0, @interval, 0, @q) { }
        @src.should be_kind_of(Dispatch::Source)
      end

      it "should not be suspended" do
        @src = Dispatch::Source.timer(0, @interval, 0, @q) { }
        @src.suspended?.should == false
      end

      it "fires after the delay" do
        delay = 2*@interval
        @latest = start = Time.now      
        @src = Dispatch::Source.timer(delay, @interval, 0, @q) do
          @latest = Time.now
        end
        @latest.should == start
        sleep delay
        @q.sync { }
        @latest.should > start
      end

      it "fires every interval thereafter" do
        repeats = 3
        @count = -1 # ignore zeroeth event to simplify interval counting
        t0 = Time.now
        @src = Dispatch::Source.timer(0, @interval, 0, @q) do |s|
          @count +=  s.data
        end
        sleep repeats*@interval
        @q.sync { }
        t1 = Time.now
        @count.should == repeats
        @count.should == ((t1-t0).to_f / @interval).to_i
      end

      it "should not repeat with TIME_FOREVER" do
        @count = 0
        @src = Dispatch::Source.timer(0, Dispatch::TIME_FOREVER, 0, @q) { @count += 1 }
        sleep 0.1
        @count.should == 1
      end

      it "should not raise an exception if passed the main queue" do
        gcdq = Dispatch::Queue.main
        lambda { @src = Dispatch::Source.timer(0, 5, 0.1, gcdq){} }.should_not raise_error
      end

    end
  end

end
