require File.dirname(__FILE__) + '/../../spec_helper'

ruby_version_is "1.9" do
  describe "Numeric#denominator" do
    # The Numeric child classes override this method, so their behaviour is
    # specified in the appropriate place
    before(:each) do
      @numbers = [
        20,             # Integer
        99999999**99,   # Bignum
      ] 
    end

    it "returns 1" do
      @numbers.each {|number| number.denominator.should == 1}
    end  
  end
end
