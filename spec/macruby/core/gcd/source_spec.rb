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
          src = Dispatch::Source.new(@type, 0, 0, @q) {|s|  @i = 42}
          src << 42
          @q.sync { }
          @i.should == 42
        end        

        it "passes source to event handler" do
          @flag = false
          src = Dispatch::Source.new(@type, 0, 0, @q) do |source|
            @flag = (source.is_a? Dispatch::Source)
          end
          src << 42
          @q.sync { }
          @flag.should == true
        end        

        it "passes data to source in event handler" do
          @i = 0
          src = Dispatch::Source.new(@type, 0, 0, @q) do |source|
            @i = source.data
          end
          src << 42
          @q.sync { }
          @i.should == 42
        end        

        it "coalesces data for source in event handler" do
          @i = 0
          src = Dispatch::Source.new(@type, 0, 0, @q) do |source|
            @i = source.data
          end
          src.suspend!
          src << 17
          src << 25
          src.resume!
          @q.sync { }
          @i.should == 42
        end        

        it "runs cancel handler when cancelled" do
          @i = 0
          src = Dispatch::Source.new(@type, 0, 0, @q) {|s|  @i = 21}
          src.on_cancel { @i = 42 }
          src.cancel!
          @q.sync { }
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
          src = Dispatch::Source.new(@type, 0, 0, @q) {|s|  @i = 42}
          src << 42
          @q.sync { }
          @i.should == 42
        end        
        
        it "coalesces data for source in event handler" do
          @i = 0
          src = Dispatch::Source.new(@type, 0, 0, @q) do |source|
            @i = source.data
          end
          src.suspend!
          src << 0b000_010
          src << 0b101_000
          src.resume!
          @q.sync { }
          @i.should == 42 #0b101_010
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
        end

        it "fires on process event with event mask data" do
          @i = 0
          src = Dispatch::Source.new(@type, $$, @mask, @q) { |s|  @i = s.data }
          Signal.trap(@signal, "IGNORE")
          Process.kill(@signal, $$)
          Signal.trap(@signal, "DEFAULT")
          @q.sync { }
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
        end

        it "fires on signal with signal count data" do
          @i = 0
          src = Dispatch::Source.new(@type, @signal, 0, @q) { |s|  @i = s.data }
          Signal.trap(@signal, "IGNORE")
          Process.kill(@signal, $$)
          Signal.trap(@signal, "DEFAULT")
          @q.sync { }
          @i.should == 1
          src.cancel!
        end
      end
      
      describe "file:" do
        before :each do
          @filename = "/var/tmp/gcd_spec_source-#{$$}"
          @msg = "#{$$}: #{Time.now}"
        end
      
        describe :READ do
          before :each do
            @type = Dispatch::Source::READ
          end

          it "returns an instance of Dispatch::Source" do
            src = Dispatch::Source.new(@type, $stdin.to_i, 0, @q) { }
            src.should be_kind_of(Dispatch::Source)
          end
          
          it "fires with data on estimate of readable bytes" do
            @result = ""
            File.delete(@filename) if File.exist?(@filename)
            File.open(@filename, "w") {|f| f.puts @msg}
            file = File.open(@filename, "r")
            src = Dispatch::Source.new(@type, file.to_i, 0, @q) do |s|
              begin
                puts "Reading #{s.data} bytes"
                @result << file.read_nonblock(s.data-1)
                puts "#{@result}: #{s.data}"
              rescue Exception => error
                puts "OOPS!: #{error}"
                src.cancel!            
              end
            end
            src.on_cancel { file.close } #is this a race condition?
            while (@result.size < @msg.size) do; end
            @q.sync { }
            @result.should == @msg
          end
        end    

        describe :WRITE do
          before :each do
            @type = Dispatch::Source::WRITE
          end

          it "returns an instance of Dispatch::Source" do
            src = Dispatch::Source.new(@type, $stdout.to_i, 0, @q) { }
            src.should be_kind_of(Dispatch::Source)
          end
        end    

        describe :VNODE do
          before :each do
            @type = Dispatch::Source::VNODE
            @mask = Dispatch::Source::VNODE_WRITE
          end

          it "returns an instance of Dispatch::Source" do
            src = Dispatch::Source.new(@type, $stdout.to_i, @mask, @q) { }
            src.should be_kind_of(Dispatch::Source)
          end
        end
  
      end
    end
      
  end
  
  describe "Dispatch::Timer" do
  end

end
