require File.dirname(__FILE__) + '/../../spec_helper'
require File.dirname(__FILE__) + '/fixtures/classes'


describe "Array#sample" do
  ruby_version_is "1.9" do
    it "should select an value from the array" do
      a = [1,2,3,4]
      b = [4,3,2,1]
      10.times {
        b.include?(a.sample).should be_true
      }
    end

    it "should return a nil value if the array is empty" do
      a = []
      a.sample.should be_nil
    end
  end
end
