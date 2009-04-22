require File.dirname(__FILE__) + '/../../spec_helper'
require File.dirname(__FILE__) + '/fixtures/classes'

describe "Array#choice" do
  ruby_version_is "" .. "1.9" do
    it "should select an value from the array" do
      a = [1,2,3,4]
      b = [4,3,2,1]
      10.times {
        b.include?(a.choice).should be_true
      }
    end

    it "should return a nil value if the array is empty" do
      a = []
      a.choice.should be_nil
    end
  end

  ruby_version_is "1.9" do
    it "should raise NoMethodError in ruby 1.9" do
      a = [1,2,3,4]
      lambda { a.choice }.should raise_error(NoMethodError)
    end
  end
end
