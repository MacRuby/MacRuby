require File.dirname(__FILE__) + '/../../spec_helper'

describe "Encoding.find" do
  it "returns the Encoding instance for the given name" do
    encoding = Encoding.find("UTF-8")
    encoding.should be_kind_of(Encoding)
    encoding.name.should == "UTF-8"
  end

  it "raises an ArgumentError if given a name for a non-existing Encoding" do
    lambda {
      Encoding.find("encoding-does-not-exist")
    }.should raise_error(ArgumentError)
  end

  it "coerces any object to a String if it responds to #to_str" do
    o = Object.new
    def o.to_str; "UTF-8"; end

    Encoding.find(o).name.should == "UTF-8"
  end
end
