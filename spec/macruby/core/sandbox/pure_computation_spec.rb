require File.expand_path(File.dirname(__FILE__) + '/spec_helper')

describe Sandbox, ".pure_computation" do
  
  it_behaves_like :sandbox_no_write, :no_write
  it_behaves_like :sandbox_no_network, :no_network
  
  before do
    add_line "framework 'Cocoa'"
    add_line "Sandbox.pure_computation.apply!"
  end
  
  it "should cause Kernel#open to fail with Errno::EPERM" do
    with_temporary_file do |temp|
      add_line "open('#{temp}')"
      result.should =~ /Errno::EPERM/
    end
  end
  
  it "should cause Kernel#require to raise a LoadError" do
    add_line "require '#{File.expand_path(File.dirname(__FILE__) + '/spec_helper')}'"
    result.should =~ /LoadError/
  end
  
  after :each do
    ScratchPad.clear
  end
  
end