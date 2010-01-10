require File.dirname(__FILE__) + '/../../spec_helper'

ruby_version_is "1.9" do
  describe "Math.gamma" do
    it "returns a float" do
      Math.gamma(2).should be_kind_of(Float)
    end

    it "returns well-known values" do
      # These are theoretically known precise values
      Math.gamma(0).infinite?.should == 1
      Math.gamma(0.5).should be_close(Math.sqrt(Math::PI), TOLERANCE)
      Math.gamma(1).should be_close(1.0, TOLERANCE)
      Math.gamma(6.0).should be_close(120.0, TOLERANCE)
      Math.gamma(1.0/0).infinite?.should == 1
    end
    
    it "returns good numerical approximations" do
      Math.gamma( 3.2) .should be_close(         2.423965, TOLERANCE)
      Math.gamma(-2.15).should be_close(        -2.999619, TOLERANCE)
      Math.gamma( 0.00001).should be_close(  99999.422794, TOLERANCE)
      Math.gamma(-0.00001).should be_close(-100000.577225, TOLERANCE)
    end
    
    it "raises Domain Error on negative integers" do
      lambda { Math.gamma(-1) }.should raise_error(Errno::EDOM)
      lambda { Math.gamma(-2.0) }.should raise_error(Errno::EDOM)
    end
    
    it "raises Domain Error given negative infinity" do
      lambda { Math.gamma(-1.0/0) }.should raise_error(Errno::EDOM)
    end
    
    it "returns NaN given NaN" do
      Math.gamma(0.0/0).nan?.should be_true
    end

  end
end