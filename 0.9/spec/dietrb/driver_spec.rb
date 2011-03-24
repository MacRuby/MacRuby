require File.expand_path('../spec_helper', __FILE__)
require 'irb/driver'

describe "IRB::Driver" do
  before :all do
    @driver = StubDriver.new
    IRB::Driver.current = @driver
  end
  
  it "assigns the driver for the current thread" do
    Thread.current[:irb_driver].should == @driver
  end
  
  it "returns the same driver for child threads" do
    Thread.new do
      IRB::Driver.current = other = StubDriver.new
      Thread.new { IRB::Driver.current.should == other }.join
    end.join
    Thread.new { IRB::Driver.current.should == @driver }.join
  end
end

describe "IRB::Driver::OutputRedirector" do
  before :each do
    @driver = StubDriver.new
    @driver.output = OutputStub.new
    IRB::Driver.current = @driver
    
    @redirector = IRB::Driver::OutputRedirector.new
  end
  
  it "returns $stderr as the target if no current driver could be found" do
    IRB::Driver.current = nil
    IRB::Driver::OutputRedirector.target.should == $stderr
  end
  
  it "returns the current driver's output as the target" do
    IRB::Driver::OutputRedirector.target.should == @driver.output
  end
  
  it "forwards method calls to the current target" do
    @redirector.send_to_target(:eql?, @driver.output).should == true
  end
  
  it "writes to the current target's output" do
    @redirector.write("strawberry coupe")
    @driver.output.printed.should == "strawberry coupe"
  end
  
  it "returns the amount of bytes written" do
    @redirector.write("banana coupe").should == 12
  end
  
  it "coerces an object to a string before writing" do
    o = Object.new
    def o.to_s; "cherry coupe"; end
    @redirector.write(o)
    @driver.output.printed.should == "cherry coupe"
  end
  
  it "forwards puts to the current target's output" do
    @redirector.puts("double", "coupe")
    @driver.output.printed.should == "double\ncoupe\n"
  end
end