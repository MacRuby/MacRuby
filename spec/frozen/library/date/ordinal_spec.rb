require 'date' 
require File.dirname(__FILE__) + '/../../spec_helper'
require File.dirname(__FILE__) + '/shared/commercial'

describe "Date#ordinal" do

  ruby_version_is "" ... "1.9" do
    it "should be able to construct a Date object from an ordinal date" do
      lambda { Date.ordinal(1582, 287) }.should raise_error(ArgumentError)
      Date.ordinal(1582, 288).should == Date.civil(1582, 10, 15)
      Date.ordinal(1582, 287, Date::ENGLAND).should == Date.civil(1582, 10, 14, Date::ENGLAND)
    end
  end

  ruby_version_is "1.9" do
    it "should be able to construct a Date object from an ordinal date" do
      Date.ordinal(1582, 288).should == Date.civil(1582, 10, 25)
      Date.ordinal(1582, 287, Date::ENGLAND).should == Date.civil(1582, 10, 14, Date::ENGLAND)
    end
  end

end

describe "Date#valid_ordinal?" do

  ruby_version_is "" ... "1.9" do
    it "should be able to determine if the date is a valid ordinal date" do
      Date.valid_ordinal?(1582, 287).should == nil
      Date.valid_ordinal?(1582, 288).should == Date.civil(1582, 10, 15).jd
      Date.valid_ordinal?(1582, 287, Date::ENGLAND).should_not == nil
      Date.valid_ordinal?(1582, 287, Date::ENGLAND).should == Date.civil(1582, 10, 14, Date::ENGLAND).jd
    end
  
    it "should be able to handle negative day numbers" do
      Date.valid_ordinal?(1582, -79).should == nil
      Date.valid_ordinal?(2007, -100).should == Date.valid_ordinal?(2007, 266)
    end
  end

  ruby_version_is "1.9" do
    it "should be able to determine if the date is a valid ordinal date" do
      Date.valid_ordinal?(1582, 287).should == true 
      Date.valid_ordinal?(1582, 288).should == true
      Date.valid_ordinal?(1582, 287, Date::ENGLAND).should == true
      Date.valid_ordinal?(1582, 287, Date::ENGLAND).should == true
    end
  
    it "should be able to handle negative day numbers" do
      Date.valid_ordinal?(1582, -79).should == true
      Date.valid_ordinal?(2007, -100).should == true
    end
  end
end
