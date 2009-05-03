# encoding: utf-8

require File.dirname(__FILE__) + '/../../spec_helper'

describe "Encoding.compatible?" do
  before :all do
    @ascii1 = "ant"
    @ascii2 = "bee"
    @iso = "\xee"
    @iso.force_encoding(Encoding::ISO_8859_1)
    @utf = "δog"
  end

  it "returns an Encoding instance which will be compatible with both given strings" do
    encoding = Encoding.compatible?(@ascii1, @ascii2)
    encoding.should be_kind_of(Encoding)
    encoding.name.should == "UTF-8"

    encoding = Encoding.compatible?(@ascii1, @iso)
    encoding.should be_kind_of(Encoding)
    encoding.name.should == "ISO-8859-1"

    encoding = Encoding.compatible?(@ascii1, @utf)
    encoding.should be_kind_of(Encoding)
    encoding.name.should == "UTF-8"
  end

  it "returns `nil' if no compatible Encoding for the two given strings exists" do
    Encoding.compatible?(@iso, @utf).should be_nil
  end
end
