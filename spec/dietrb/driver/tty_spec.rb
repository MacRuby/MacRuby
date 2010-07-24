require File.expand_path('../../spec_helper', __FILE__)
require 'irb/driver/tty'

describe "IRB::Driver::TTY" do
  before do
    @driver = IRB::Driver::TTY.new(InputStub.new, OutputStub.new)
    @context = IRB::Context.new(Object.new)
    @driver.context_stack << @context
  end
  
  it "prints the prompt and reads a line of input" do
    @driver.input.stub_input "calzone"
    @driver.readline.should == "calzone"
    @driver.output.printed.should == @context.prompt
  end
  
  it "consumes input" do
    @driver.input.stub_input "calzone"
    @driver.consume.should == "calzone"
  end
  
  it "clears the context buffer if an Interrupt signal is received while consuming input" do
    @context.process_line("class A")
    def @driver.readline; raise Interrupt; end
    @driver.consume.should == ""
    @context.source.to_s.should == ""
  end
end

describe "IRB::Driver::TTY, when starting the runloop" do
  before do
    @driver = IRB::Driver::TTY.new(InputStub.new, OutputStub.new)
    IRB::Driver.current = @driver
    @context = IRB::Context.new(Object.new)
  end
  
  it "makes the given context the current one, for this driver, for the duration of the runloop" do
    $from_context = nil
    @driver.input.stub_input "$from_context = IRB::Driver.current.context"
    @driver.run(@context)
    $from_context.should == @context
    IRB::Driver.current.context.should == nil
  end
  
  it "feeds input into a given context" do
    $from_context = false
    @driver.input.stub_input "$from_context = true", "exit"
    @driver.run(@context)
    $from_context.should == true
  end
  
  it "makes sure there's a global output redirector while running a context" do
    before = $stdout
    $from_context = nil
    @driver.input.stub_input "$from_context = $stdout", "exit"
    @driver.run(@context)
    $from_context.class == IRB::Driver::OutputRedirector
    $stdout.should == before
  end
end

# describe "Kernel::irb" do
#   it "creates a new context for the given object and runs it" do
#     IRB.io = CaptureIO.new
#     IRB.io.stub_input("::IRBRan = self")
#     o = Object.new
#     irb(o)
#     IRBRan.should == o
#   end
# end