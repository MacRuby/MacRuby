require File.expand_path('../../spec_helper', __FILE__)
require 'readline'
require "tempfile"

require 'irb/ext/history'
require 'irb/driver'

describe "IRB::History, by default," do
  it "stores the history in ~/.irb_history" do
    IRB::History.file.should == File.expand_path("~/.irb_history")
  end
end

describe "IRB::History" do
  before do
    @file = Tempfile.new("irb_history.txt")
    IRB::History.file = @file.path
    @history = IRB::History
  end
  
  after do
    @file.close
  end
  
  it "adds input to the history file" do
    @history.input "puts :ok"
    @file.rewind; @file.read.should == "puts :ok\n"
    @history.input "foo(x)"
    @file.rewind; @file.read.should == "puts :ok\nfoo(x)\n"
  end
  
  it "returns the same input value" do
    @history.input("foo(x)").should == "foo(x)"
  end
  
  it "returns the contents of the history file as an array of lines" do
    @history.input "puts :ok"
    @history.to_a.should == ["puts :ok"]
    @history.input "foo(x)"
    @history.to_a.should == ["puts :ok", "foo(x)"]
  end
  
  it "returns an empty array if the history file doesn't exist yet and create it once input is added" do
    @file.close
    FileUtils.rm(@file.path)
    
    @history.to_a.should == []
    File.exist?(@file.path).should == false
    
    @history.input "puts :ok"
    File.exist?(@file.path).should == true
    @history.to_a.should == ["puts :ok"]
  end
  
  it "stores the contents of the history file in Readline::HISTORY once" do
    Readline::HISTORY.clear
    
    @history.input "puts :ok"
    @history.input "foo(x)"
    
    IRB::History.setup
    IRB::History.setup
    
    Readline::HISTORY.to_a.should == ["puts :ok", "foo(x)"]
  end
  
  it "clears the history and history file" do
    @history.input "puts :ok"
    @history.input "foo(x)"
    @history.clear!
    
    @file.rewind; @file.read.should == ""
    Readline::HISTORY.to_a.should == []
  end
end

module IRB::History
  class << self
    def clear!
      @cleared = true
    end
    
    def cleared?
      @cleared
    end
  end
end

describe "IRB::History, concerning the user api, by default," do
  it "shows a maximum of 50 history entries" do
    IRB::History.max_entries_in_overview.should == 50
  end
end

describe "IRB::History, concerning the user api," do
  before do
    sources = [
      "puts :ok",
      "x = foo(x)",
      "class AAA",
      "  def bar",
      "    :ok",
      "  end",
      "end",
      "THIS LINE REPRESENTS THE ENTERED COMMAND AND SHOULD BE OMITTED!"
    ]
    
    Readline::HISTORY.clear
    sources.each { |source| Readline::HISTORY.push(source) }
    
    IRB::History.max_entries_in_overview = 5
    
    IRB::History.extend(OutputStubMixin)
    IRB::History.clear_printed!
    
    @context = IRB::Context.new(Object.new)
    @context.extend(OutputStubMixin)
    IRB::Driver.current = StubDriver.new(@context)
  end
  
  it "returns nil so that IRB doesn't cache some arbitrary line number" do
    history.should == IRB::Context::IGNORE_RESULT
  end
  
  it "prints a formatted list with IRB::History.max_entries_in_overview number of history entries" do
    history
    
    IRB::History.printed.should == %{
2: class AAA
3:   def bar
4:     :ok
5:   end
6: end
}.sub(/\n/, '')
  end
  
  it "prints a formatted list of N most recent history entries" do
    history(7)
    
    IRB::History.printed.should == %{
0: puts :ok
1: x = foo(x)
2: class AAA
3:   def bar
4:     :ok
5:   end
6: end
}.sub(/\n/, '')
  end
  
  it "prints a formatted list of all history entries if the request number of entries is more than there is" do
    history(777)

    IRB::History.printed.should == %{
0: puts :ok
1: x = foo(x)
2: class AAA
3:   def bar
4:     :ok
5:   end
6: end
}.sub(/\n/, '')
  end
  
  it "evaluates the history entry specified" do
    @context.__evaluate__("x = 2; def foo(x); x * 2; end")
    history! 1
    @context.__evaluate__("x").should == 4
  end
  
  it "evaluates the history entries specified by a range" do
    history! 2..6
    @context.__evaluate__("AAA.new.bar").should == :ok
  end
  
  it "clears the history and history file" do
    clear_history!
    IRB::History.cleared?.should == true
  end
end