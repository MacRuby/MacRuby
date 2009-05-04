require File.dirname(__FILE__) + '/../../spec_helper'

describe "Encoding#names" do
  it "returns an array of names by which an Encoding instance can be referenced" do
    Encoding.find("UTF-8").names.should == %w{ UTF-8 CP65001 locale external }
    Encoding.find("ASCII").names.should == %w{ US-ASCII ASCII ANSI_X3.4-1968 646 }
  end
end
