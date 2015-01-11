require File.dirname(__FILE__) + "/../spec_helper"
FixtureCompiler.require! "exception"
TestException # force dynamic load

describe "An Objective-C exception" do
  before do
    begin
      @line = __LINE__ + 1
      NSArray.array.objectAtIndex(0)
    rescue Exception => e
      @exception = e
    end
  end

  it "can be catched from Ruby" do
    @exception.class.should == Exception
    @exception.class.should == NSException
  end

  it "returns the `reason' from #message" do
    @exception.message.should == "*** -[NSArray objectAtIndex:]: index (0) beyond bounds (0)"
  end

  describe "returned from Objective-C code" do
    it "returns the `callStackSymbols' from #backtrace" do
      backtrace = TestException.catchObjCException.backtrace
      backtrace.class.should == Array
      entry = backtrace.find do |line|
        line.include?('exception.bundle') && line.include?('+[TestException catchObjCException]')
      end
      entry.should_not == nil
    end
  end
end

describe "A Ruby exception" do
  before do
    @o = Object.new
    def @o.raiseRubyException
      raise ArgumentError, "Where would the world be without arguments?"
    end
  end

  it "can be catched from Objective-C" do
    TestException.catchRubyException(@o).class.should == ArgumentError
    $!.should == nil
  end

  it "returns the class name as the exception name" do
    TestException.catchRubyExceptionAndReturnName(@o).should == "ArgumentError"
  end

  it "returns the message as the exception reason" do
    TestException.catchRubyExceptionAndReturnReason(@o).should == "Where would the world be without arguments?"
  end
end
