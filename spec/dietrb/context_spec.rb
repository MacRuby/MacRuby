require File.expand_path('../spec_helper', __FILE__)
require 'tempfile'

main = self

describe "IRB::Context" do
  before do
    @output = setup_current_driver.output
    @context = IRB::Context.new(main)
    @context.extend(OutputStubMixin)
  end
  
  it "initializes with an object and stores a copy of its binding" do
    @context.object.should == main
    eval("self", @context.binding).should == main
    eval("x = :ok", @context.binding)
    eval("y = x", @context.binding)
    eval("y", @context.binding).should == :ok
  end
  
  it "initializes with an object and an explicit binding" do
    context = IRB::Context.new(Object.new, TOPLEVEL_BINDING)
    eval("class InTopLevel; end", context.binding)
    lambda { ::InTopLevel }.should_not raise_error(NameError)
  end
  
  it "initializes with an 'empty' state" do
    @context.line.should == 1
    @context.source.class.should == IRB::Source
    @context.source.to_s.should == ""
  end
  
  it "does not use the same binding copy of the top level object" do
    lambda { eval("x", @context.binding) }.should raise_error(NameError)
  end
  
  it "prints to the output object of the current driver" do
    @context.output("croque monsieur")
    @output.printed.should == "croque monsieur\n"
    @context.printed.should == ""
  end

  it "prints as normal when no current driver is available" do
    IRB::Driver.current = nil
    @context.output("croque monsieur")
    @output.printed.should == ""
    @context.printed.should == "croque monsieur\n"
  end
end

describe "IRB::Context, when evaluating source" do
  before do
    @output = setup_current_driver.output
    @context = IRB::Context.new(main)
    IRB.formatter = IRB::Formatter.new
  end
  
  it "evaluates code with the object's binding" do
    @context.__evaluate__("self").should == main
  end
  
  it "prints the result" do
    @context.evaluate("Hash[:foo, :foo]")
    @output.printed.should == "=> {:foo=>:foo}\n"
  end
  
  it "assigns the result to the local variable `_'" do
    result = @context.evaluate("Object.new")
    @context.evaluate("_").should == result
    @context.evaluate("_").should == result
  end
  
  it "coerces the given source to a string first" do
    o = Object.new
    def o.to_s; "self"; end
    @context.evaluate(o).should == main
  end
  
  it "rescues any type of exception" do
    lambda {
      @context.evaluate("DoesNotExist")
      @context.evaluate("raise Exception")
    }.should_not.raise_error
  end
  
  it "assigns the last raised exception to the variables `exception' / `e'" do
    @context.evaluate("DoesNotExist")
    @context.__evaluate__("exception").class.should == NameError
    @context.__evaluate__("exception").message.should include('DoesNotExist')
    @context.__evaluate__("e").should == @context.__evaluate__("exception")
  end
  
  it "prints the exception that occurs" do
    @context.evaluate("DoesNotExist")
    @output.printed.should =~ /^NameError:.+DoesNotExist/
  end
  
  it "uses the line number of the *first* line in the buffer, for the line parameter of eval" do
    @context.process_line("DoesNotExist")
    @output.printed.should =~ /\(irb\):1:in/
    @context.process_line("class A")
    @context.process_line("DoesNotExist")
    @context.process_line("end")
    @output.printed.should =~ /\(irb\):3:in.+\(irb\):2:in/m
  end
  
  it "ignores the result if it's IRB::Context::IGNORE_RESULT" do
    @context.evaluate(":bananas")
    @context.evaluate("IRB::Context::IGNORE_RESULT").should == nil
    @output.printed.should == "=> :bananas\n"
    @context.evaluate("_").should == :bananas
  end
end

describe "IRB::Context, when receiving input" do
  before do
    @output = setup_current_driver.output
    @context = IRB::Context.new(main)
    @context.extend(InputStubMixin)
  end
  
  it "adds the received code to the source buffer" do
    @context.process_line("def foo")
    @context.process_line("p :ok")
    @context.source.to_s.should == "def foo\np :ok"
  end
  
  it "clears the source buffer" do
    @context.process_line("def foo")
    @context.clear_buffer
    @context.source.to_s.should == ""
  end
  
  it "increases the current line number" do
    @context.line.should == 1
    @context.process_line("def foo")
    @context.line.should == 2
    @context.process_line("p :ok")
    @context.line.should == 3
  end
  
  it "evaluates the buffered source once it's a valid code block" do
    def @context.evaluate(source); @evaled = source; end
    
    @context.process_line("def foo")
    @context.process_line(":ok")
    @context.process_line("end; p foo")
    
    source = @context.instance_variable_get(:@evaled)
    source.to_s.should == "def foo\n:ok\nend; p foo"
  end
  
  it "prints that a syntax error occurred on the last line and reset the buffer to the previous line" do
    @context.process_line("def foo")
    @context.process_line("  };")
    
    @context.source.to_s.should == "def foo"
    @output.printed.should == "SyntaxError: compile error\n(irb):2: syntax error, unexpected '}'\n"
  end
  
  it "returns whether or not the runloop should continue, but only if the level is 0" do
    @context.process_line("def foo").should == true
    @context.process_line("quit").should == true
    @context.process_line("end").should == true
    
    @context.process_line("quit").should == false
  end
  
  it "inputs a line to be processed" do
    expected = "#{@context.formatter.prompt(@context)}2 * 21\n=> 42\n"
    @context.input_line("2 * 21")
    @output.printed.should == expected
  end
end
