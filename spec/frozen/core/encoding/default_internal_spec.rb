require File.dirname(__FILE__) + '/../../spec_helper'

describe "Encoding.default_internal" do
  it "returns `nil' by default" do
    Encoding.default_internal.should be_nil
  end
end

describe "Encoding.default_internal=" do
  after :each do
    Encoding.default_internal = nil
  end

  it "takes an Encoding instance" do
    encoding = Encoding.find('macRoman')
    Encoding.default_internal = encoding
    Encoding.default_internal.name.should == 'macRoman'
  end

  it "takes a string name of an encoding" do
    Encoding.default_internal = 'macRoman'
    Encoding.default_internal.name.should == 'macRoman'
  end

  it "assigns the default internal encoding to be used for IO" do
    Encoding.default_internal = 'macRoman'
    open(__FILE__) do |file|
      file.internal_encoding.name.should == 'macRoman'
    end
  end
end
