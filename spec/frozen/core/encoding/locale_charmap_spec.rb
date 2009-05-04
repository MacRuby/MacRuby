require File.dirname(__FILE__) + '/../../spec_helper'

describe "Encoding.locale_charmap" do
  it "returns the name of the charmap of the current environment's locale" do
    Encoding.locale_charmap.should == ENV["LANG"].split('.').last
  end
end
