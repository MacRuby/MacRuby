require File.dirname(__FILE__) + "/../spec_helper"

describe "A MacRuby Float object" do
  it "returns the same object_id than another equal Float literal" do
    o1 = 1.0
    o2 = 1.0
    o1.object_id.should == o2.object_id
  end
end
