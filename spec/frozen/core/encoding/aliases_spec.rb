require File.dirname(__FILE__) + '/../../spec_helper'

describe "Encoding.aliases" do
  it "returns a hash where the keys are the aliases and the values the real encoding name" do
    Encoding.aliases.should be_kind_of(Hash)
    Encoding.aliases["BINARY"].should == "ASCII-8BIT"
  end
end
