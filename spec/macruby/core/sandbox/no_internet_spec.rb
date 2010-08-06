require File.expand_path(File.dirname(__FILE__) + '/spec_helper')

describe Sandbox, ".no_network" do
  
  before do
    add_line "framework 'Cocoa'"
    add_line "Sandbox.no_network.apply!"
  end
  
  it_behaves_like :sandbox_no_network, :no_network
  
  after do
    ScratchPad.clear
  end
  
end