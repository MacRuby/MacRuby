require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')

describe "Sandbox.pure_computation" do
  
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
  
  it "should be frozen" do
    Sandbox.pure_computation.frozen?.should be_true
  end
end