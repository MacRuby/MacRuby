require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  
  $global = 0
  class Actee
    def initialize(s); @s = s; end
    def current_queue; Dispatch::Queue.current; end
    def delay_set(n); sleep 0.01; $global = n; end
    def increment(v); v+1; end
    def to_s; @s; end
  end
  
  describe "Dispatch method" do
    before :each do
      @actee = Actee.new("my_actee")
    end

    describe :sync do
      it "should execute the block Synchronously" do
        $global = 0
        Dispatch::sync { @actee.delay_set(42) }
        $global.should == 42
      end
    end

    describe :async do
      it "should execute the block Asynchronously" do
        $global = 0
        Dispatch::async { @actee.delay_set(42) }
        $global.should == 0
        while $global == 0 do; end
        $global.should == 42      
      end
    end
    
  end
end