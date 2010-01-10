require File.dirname(__FILE__) + '/../../spec_helper'

ruby_version_is "1.9" do
  describe "Math.lgamma" do
    it "returns an array of 2 elements" do
      lg = Math.lgamma(0)
      lg.should be_kind_of(Array)
      lg.size.should == 2
    end

    it "returns known values" do
      Math.lgamma(0).should  == [1.0/0, 1]
      Math.lgamma(-1).should == [1.0/0, 1]
      lg1 = Math.lgamma(0.5)
      lg1[0].should be_close(Math.log(Math.sqrt(Math::PI)), TOLERANCE)
      lg1[1].should == 1
      
      lg2 = Math.lgamma(6.0)
      lg2[0].should be_close(Math.log(120.0), TOLERANCE)
      lg2[1].should == 1
    end

    it "returns good numerical approximations" do
      lg1 = Math.lgamma(-0.5)
      lg1[0].should be_close(1.2655121, TOLERANCE)
      lg1[1].should == -1
      
      lg2 = Math.lgamma(-1.5)
      lg2[0].should be_close(0.8600470, TOLERANCE)
      lg2[1].should == 1
    end
    
    it "returns correct values given +/- infinity" do
      Math.lgamma( 1.0/0).should == [1.0/0, 1]
      Math.lgamma(-1.0/0).should == [1.0/0, 1]
    end
    
    it "returns correct value given NaN" do
      Math.lgamma(0.0/0)[0].nan?.should be_true
      Math.lgamma(0.0/0)[1].should == 1
    end

  end
end