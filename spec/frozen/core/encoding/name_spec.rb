require File.dirname(__FILE__) + '/../../spec_helper'

describe "Encoding#name" do
  it "returns the name of the Encoding instance" do
    Encoding.find("UTF-8").name.should == "UTF-8"
    Encoding.find("ASCII").name.should == "US-ASCII"
  end
end
