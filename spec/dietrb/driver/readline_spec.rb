require File.expand_path("../../spec_helper", __FILE__)
require "irb/driver/readline"

module Readline
  extend InputStubMixin
  extend OutputStubMixin

  def self.input=(input)
    @given_input = input
  end

  def self.given_input
    @given_input
  end

  def self.output=(output)
    @given_output = output
  end

  def self.given_output
    @given_output
  end

  def self.use_history=(use_history)
    @use_history = use_history
  end

  def self.use_history
    @use_history
  end

  def self.readline(prompt, use_history)
    @use_history = use_history
    print prompt
    @input.shift
  end
end

describe "IRB::Driver::Readline" do
  before do
    @driver = IRB::Driver::Readline.new(InputStub.new, OutputStub.new)
    @context = IRB::Context.new(Object.new)
    @driver.context_stack << @context
  end

  it "is a subclass of IRB::Driver::TTY" do
    IRB::Driver::Readline.superclass.should == IRB::Driver::TTY
  end

  it "assigns the given input and output to the Readline module" do
    Readline.given_input.should == @driver.input
    Readline.given_output.should == @driver.output
  end

  it "assigns a completion object" do
    Readline.completion_proc.class.should == IRB::Completion
  end

  it "reads a line through the Readline module" do
    Readline.stub_input "nom nom nom"
    @driver.readline.should == "nom nom nom"
  end

  it "tells the Readline module to use the history" do
    Readline.use_history = false
    Readline.stub_input "nom nom nom"
    @driver.readline
    Readline.use_history.should == true
  end
end
