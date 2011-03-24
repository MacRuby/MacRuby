require File.expand_path(File.dirname(__FILE__) + '/spec_helper')

describe Sandbox, ".no_writes" do
  
  before do
    add_line "framework 'Cocoa'"
    add_line "Sandbox.no_writes.apply!"
  end
  
  it_behaves_like :sandbox_no_write, :no_write
  
  it "prevents writing to a file in /tmp" do
    with_temporary_file("/tmp/sandboxed") do
      add_line "open('/tmp/sandboxed', 'w').puts 'This must fail'"
      result.should =~ /Errno::EPERM/
    end
  end
  
  after do
    ScratchPad.clear
  end
  
end