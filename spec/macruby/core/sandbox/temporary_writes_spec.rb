require File.expand_path(File.dirname(__FILE__) + '/spec_helper')

describe Sandbox, ".temporary_writes" do
  
  before do
    add_line "framework 'Cocoa'"
    add_line "Sandbox.temporary_writes.apply!"
  end
  
  it_behaves_like :sandbox_no_write, :no_write
  
  it "should allow writing to a file in /tmp" do
    with_temporary_file("/tmp/sandboxed") do
      add_line "open('/tmp/sandboxed', 'w').puts 'This must succeed'"
      add_line "print 'Success'"
      result.should == "Success"
    end
  end
  
  after do
    ScratchPad.clear
  end
  
end