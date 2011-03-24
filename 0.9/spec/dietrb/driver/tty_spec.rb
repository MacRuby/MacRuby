require File.expand_path('../../spec_helper', __FILE__)
require 'irb/driver/tty'

main = self

describe "IRB::Driver::TTY" do
  before do
    @driver = IRB::Driver::TTY.new(InputStub.new, OutputStub.new)
    @context = IRB::Context.new(main)
    @driver.context_stack << @context
  end

  after do
    @context.formatter.auto_indent = false
  end

  it "prints the prompt and reads a line of input" do
    @context.process_line("def foo")
    @driver.input.stub_input("calzone")
    @driver.readline.should == "calzone"
    @driver.output.printed.should == @context.prompt
  end

  it "prints a prompt with indentation if it's configured" do
    @context.formatter.auto_indent = true
    @context.process_line("def foo")
    @driver.input.stub_input("calzone")
    @driver.readline
    @driver.output.printed[-2,2].should == "  "
  end
  
  it "consumes input" do
    @driver.input.stub_input("calzone")
    @driver.consume.should == "calzone"
  end
  
  it "clears the context buffer if an Interrupt signal is received while consuming input" do
    @context.process_line("class A")
    def @driver.readline; raise Interrupt; end
    @driver.consume.should == ""
    @context.source.to_s.should == ""
  end

  it "feeds the input into the context" do
    @driver.process_input("def foo")
    @context.source.to_s.should == "def foo"
  end

  it "updates the previously printed line on the console, if a change to the input occurs (such as re-indenting)" do
    @context.formatter.auto_indent = true
    @driver.process_input("def foo")
    @driver.process_input("p :ok")
    @driver.process_input("  end")
    @driver.output.printed.strip.should == [
      IRB::Driver::TTY::CLEAR_LAST_LINE + "irb(main):002:1>   p :ok",
      IRB::Driver::TTY::CLEAR_LAST_LINE + "irb(main):003:0> end"
    ].join("\n")
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
    @driver.input.stub_input("$from_context = IRB::Driver.current.context")
    @driver.run(@context)
    $from_context.should == @context
    IRB::Driver.current.context.should == nil
  end
  
  it "feeds input into a given context" do
    $from_context = false
    @driver.input.stub_input("$from_context = true", "exit")
    @driver.run(@context)
    $from_context.should == true
  end
  
  it "makes sure there's a global output redirector while running a context" do
    before = $stdout
    $from_context = nil
    @driver.input.stub_input("$from_context = $stdout", "exit")
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
