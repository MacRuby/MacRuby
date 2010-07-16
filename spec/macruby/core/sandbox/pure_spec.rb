require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')

describe "Sandbox.pure_computation" do
  
  # More specs coming to this space soon. Right now applying a sandbox profile
  # inside a spec causes all subsequent specs to fail.
  
  it "should be frozen" do
    Sandbox.pure_computation.frozen?.should be_true
  end
end