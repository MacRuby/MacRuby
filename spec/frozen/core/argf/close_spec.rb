require File.dirname(__FILE__) + '/../../spec_helper'

describe "ARGF.close" do
  before :each do
    @file1_name = fixture __FILE__, "file1.txt"
    @file2_name = fixture __FILE__, "file2.txt"
  end

  after :each do
    ARGF.close unless ARGF.closed?
  end

  it "closes the current open stream" do
    argv [@file1_name, @file2_name] do
      io = ARGF.to_io
      ARGF.close
      io.closed?.should be_true
    end
  end
  
  it "returns self" do
    argv [@file1_name, @file2_name] do
      ARGF.close.should equal(ARGF)
    end
  end

  # This passes on 1.9, but fails on 1.8. matz claims to have fixed it in the
  # attached ticket. The actual exception raised may differ; this will need
  # checking.
  ruby_bug "#1633", "1.8" do
    it "raises an IOError if called on a closed stream" do
      argv [@file1_name] do
        lambda { ARGF.close }.should_not raise_error
        lambda { ARGF.close }.should raise_error(IOError)
      end
    end
  end

  # This passes on 1.8.6 and 1.8.7 but fails on 1.9. matz confirmed that it
  # should pass in the referenced bug report
  ruby_bug "#1633", "1.9.2" do
    it "can close STDIN" do
      argv [] do
        ARGV.size.should == 0
        ARGF.close.should == ARGF
        ARGF.closed?.should be_true
      end
    end   
  end
end
