require File.expand_path('../spec_helper', __FILE__)

describe "IRB::Source" do
  before do
    @source = IRB::Source.new
  end
  
  it "initializes with an empty buffer" do
    @source.buffer.should == []
  end
  
  it "appends source to the buffer, removing trailing newlines" do
    @source << "foo\n"
    @source << "bar\r\n"
    @source.buffer.should == %w{ foo bar }
  end
  
  it "ignores empty strings" do
    @source << ""
    @source << " \n"
    @source.buffer.should == []
  end
  
  it "removes the last line from the buffer" do
    @source << "foo\n"
    @source << "bar\r\n"
    @source.pop.should == "bar"
    @source.buffer.should == %w{ foo }
  end
  
  it "returns the full buffered source, joined by newlines" do
    @source.source.should == ""
    @source << "foo\n"
    @source.source.should == "foo"
    @source << "bar\r\n"
    @source.source.should == "foo\nbar"
  end
  
  it "aliases #to_s to #source" do
    @source << "foo"
    @source << "bar"
    @source.to_s.should == "foo\nbar"
  end
  
  it "returns that the accumulated source is a valid code block" do
    [
      ["def foo", "p :ok", "end"],
      ["class A; def", "foo(x); p x", "end; end"]
    ].each do |buffer|
      IRB::Source.new(buffer).code_block?.should == true
    end
  end
  
  it "returns that the accumulated source is not a valid code block" do
    [
      ["def foo", "p :ok"],
      ["class A; def", "foo(x); p x", "end"]
    ].each do |buffer|
      IRB::Source.new(buffer).code_block?.should == false
    end
  end
  
  it "returns whether or not the accumulated source contains a syntax error" do
    @source.syntax_error?.should == false
    @source << "def foo"
    @source.syntax_error?.should == false
    @source << "  def;"
    @source.syntax_error?.should == true
  end
  
  it "returns the current code block indentation level" do
    @source.level.should == 0
    @source << "class A"
    @source.level.should == 1
    @source << "  def foo"
    @source.level.should == 2
    @source << "    p :ok"
    @source.level.should == 2
    @source << "  end"
    @source.level.should == 1
    @source << "  class B"
    @source.level.should == 2
    @source << "    def bar"
    @source.level.should == 3
    @source << "      p :ok; end"
    @source.level.should == 2
    @source << "  end; end"
    @source.level.should == 0
  end
  
  it "caches the reflection when possible" do
    @source << "def foo"
    reflection = @source.reflect
    @source.level
    @source.code_block?
    @source.reflect.should == reflection
    
    @source << "end"
    @source.level
    new_reflection = @source.reflect
    new_reflection.should_not == reflection
    @source.code_block?
    @source.reflect.should == new_reflection
    
    reflection = new_reflection
    
    @source.pop
    @source.level
    new_reflection = @source.reflect
    new_reflection.should_not == reflection
    @source.syntax_error?
    @source.reflect.should == new_reflection
  end
end

describe "IRB::Source::Reflector" do
  def reflect(source)
    IRB::Source::Reflector.new(source)
  end
  
  it "returns whether or not the source is a valid code block" do
    reflect("def foo").code_block?.should == false
    reflect("def foo; p :ok").code_block?.should == false
    reflect("def foo; p :ok; end").code_block?.should == true
    
    reflect("if true").code_block?.should == false
    reflect("p :ok if true").code_block?.should == true
  end

  it "returns whether or not the current session should be terminated" do
    reflect("exit").terminate?.should == true
    reflect("quit").terminate?.should == true
    reflect("def foo; end; exit").terminate?.should == true
    reflect("def foo; end; quit").terminate?.should == true

    reflect("def foo; exit; end").terminate?.should == false
    reflect("def foo; quit; end").terminate?.should == false
  end
  
  it "returns whether or not the source contains a syntax error, except a code block not ending" do
    reflect("def;").syntax_error?.should == true
    reflect("def;").syntax_error?.should == true
    reflect("{ [ } ]").syntax_error?.should == true
    reflect("def foo").syntax_error?.should == false
    reflect("class A; }").syntax_error?.should == true
    reflect("class A; {" ).syntax_error?.should == false
    reflect("class A; def {").syntax_error?.should == true
    reflect("class A def foo").syntax_error?.should == true
    reflect("class A; def foo" ).syntax_error?.should == false
    reflect("def foo; {; end; }").syntax_error?.should == true
  end
  
  it "returns the actual syntax error message if one occurs" do
    reflect("def foo").syntax_error.should == nil
    reflect("}").syntax_error.should == "syntax error, unexpected '}'"
  end
  
  it "returns the code block indentation level" do
    reflect("").level.should == 0
    reflect("class A").level.should == 1
    reflect("class A; def foo").level.should == 2
    reflect("class A; def foo; p :ok").level.should == 2
    reflect("class A; def foo; p :ok; end").level.should == 1
    reflect("class A; class B").level.should == 2
    reflect("class A; class B; def bar").level.should == 3
    reflect("class A; class B; def bar; p :ok; end").level.should == 2
    reflect("class A; class B; def bar; p :ok; end; end; end").level.should == 0
  end
  
  it "correctly increases and decreases the code block indentation level for keywords" do
    [
      "class A",
      "module A",
      "def foo",
      "begin",
      "if x == :ok",
      "unless x == :ko",
      "case x",
      "while x",
      "for x in xs",
      "x.each do"
    ].each do |open|
      reflect(open).level.should == 1
      reflect("#{open}\nend").level.should == 0
    end
  end
  
  it "correctly increases and decreases the code block indentation level for literals" do
    [
      ["lambda { |x|", "}"],
      ["{ :foo => ", " :bar }"],
      ["[ 1", ", 2 ]"],

      ["'", "'"],
      ["' ", " '"],
      ["'foo ", " bar'"],
      ["' foo ", " bar '"],

      ['"', '"'],
      ['" ', ' "'],
      ['"foo ', ' bar"'],
      ['" foo ', ' bar "'],

      ["%{", "}"],
      ["%{ ", " }"],
      ["%{foo ", " bar}"],
      ["%{ foo ", " bar }"],
      ["%(foo ", " bar)"],
      ["%( foo ", " bar )"],
      ["%[ foo ", " bar ]"],
      ["%[foo ", " bar]"],

      ["%w{ ", " }"],
      ["%w{foo ", " bar}"],
      ["%w{ foo ", " bar }"],
      ["%w(foo ", " bar)"],
      ["%w( foo ", " bar )"],
      ["%w[foo ", " bar]"],
      ["%w[ foo ", " bar ]"],

      ["%W{foo ", " bar}"],
      ["%W{ foo ", " bar }"],
      ["%W(foo ", " bar)"],
      ["%W( foo ", " bar )"],
      ["%W[foo ", " bar]"],
      ["%W[ foo ", " bar ]"],

      ["%r{foo ", " bar}"],
      ["%r{ foo ", " bar }"],
      ["%r(foo ", " bar)"],
      ["%r( foo ", " bar )"],
      ["%r[foo ", " bar]"],
      ["%r[ foo ", " bar ]"],

      ["/foo ", " bar/"],
      ["/ foo ", " bar /"],
    ].each do |open, close|
      reflect(open).level.should == 1
      reflect(open).code_block?.should == false
      reflect("#{open}\n#{close}").level.should == 0
      reflect("#{open}\n#{close}").code_block?.should == true
    end
  end

  it "handles cases that contain backspaces" do
    [
      ["%{", "\b"],
      ["%w{", "\b"],
      ["%r{", "\b"],
    ].each do |open, close|
      reflect("#{open}\n#{close}").level.should == 1
      reflect("#{open}\n#{close}").code_block?.should == false
    end
  end
end
