require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')

describe "Sandbox.pure_computation" do
  
  # These tests are applicable beyond just the pure_computation spec.
  # Eventually the tests themselves will be farmed out to /sandbox/shared
  # and all sandbox specs will just be aggregations of should_behave_like calls.
  
  before(:all) do
    @filename = File.expand_path(File.dirname(__FILE__) + '/../../spec_helper.rb')
  end
  
  before(:each) do
    @code = "Sandbox.pure_computation.apply!; "
  end
  
  it "should disallow spawning new processes" do
    @code << "IO.popen('whoami')"
    ruby_exe(@code).should =~ /posix_spawn\(\) failed/
  end
  
  it "should disallow open()" do
    @code << "open('/dev/null')"
    ruby_exe(@code).should =~ /open\(\) failed/
  end
  
  it "should disallow IO.read" do
    @code << "p IO.read('/dev/urandom')"
    ruby_exe(@code).should =~ /open\(\) failed/
  end
  
  it "should disallow most NSFileManager methods" do
    @code << "p NSFileManager.defaultManager.currentDirectoryPath"
    ruby_exe(@code).should =~ /nil/
  end
  
  it "should disallow NSString.stringWithContentsOfFile" do
    @code << "p NSString.stringWithContentsOfFile('#{@filename}')"
    ruby_exe(@code).should =~ /nil/
  end
  
  it "should disallow NSString.writeToFile" do
    @code << "p 'hello'.writeToFile('#{@filename}', atomically:true)"
    ruby_exe(@code).should =~ /0/
  end
  
  it "should disallow most NSWorkspace methods" do
    @code = "framework 'Cocoa'; " + @code + "p NSWorkspace.sharedWorkspace.launchApplication('Finder')"
    ruby_exe(@code).should =~ /false/
  end
  
  it "should be frozen" do
    Sandbox.pure_computation.frozen?.should be_true
  end
end