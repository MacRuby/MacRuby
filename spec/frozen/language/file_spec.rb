require File.dirname(__FILE__) + '/../spec_helper'

# specs for __FILE__

describe "The __FILE__ constant" do
  it "equals the current filename" do
    File.basename(__FILE__).should == "file_spec.rb"
  end
  
  it "equals the full path to the file when required" do
    $:.unshift File.dirname(__FILE__) + '/fixtures'
    begin
      require 'file.rb'
      ScratchPad.recorded.should == File.dirname(__FILE__) + '/fixtures/file.rb'
    ensure
      $:.shift
    end
  end

  it "equals (eval) inside an eval" do
    eval("__FILE__").should == "(eval)"
  end
end
