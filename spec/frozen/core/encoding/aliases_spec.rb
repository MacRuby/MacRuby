require File.dirname(__FILE__) + '/../../spec_helper'

describe "Encoding.aliases" do
  it "returns a hash" do
    Encoding.aliases.should be_kind_of(Hash)
  end

  it "returns an encoding name for a given alias" do
    Encoding.aliases["BINARY"].should == "ASCII-8BIT"
  end
end
