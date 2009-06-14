require File.dirname(__FILE__) + '/../../spec_helper'
require 'rational'

describe "Rational when passed Integer, Integer" do
  it "returns a new Rational number" do
    rat = Rational(1, 2)
    rat.numerator.should == 1
    rat.denominator.should == 2
    rat.should be_an_instance_of(Rational)
    
    rat = Rational(-3, -5)
    rat.numerator.should == 3
    rat.denominator.should == 5
    rat.should be_an_instance_of(Rational)

    rat = Rational(bignum_value, 3)
    rat.numerator.should == bignum_value
    rat.denominator.should == 3
    rat.should be_an_instance_of(Rational)
  end

  it "automatically reduces the Rational" do
    rat = Rational(2, 4)
    rat.numerator.should == 1
    rat.denominator.should == 2

    rat = Rational(3, 9)
    rat.numerator.should == 1
    rat.denominator.should == 3
  end
end


# Guard against the Mathn library
conflicts_with :Prime do
  ruby_version_is ""..."1.9" do
    describe "Rational when passed Integer" do
      it "returns a new Rational number with 1 as the denominator" do
        Rational(1).should eql(Rational.new!(1, 1))
        Rational(-3).should eql(Rational.new!(-3, 1))
        Rational(bignum_value).should eql(Rational.new!(bignum_value, 1))
      end
    end
  end

  ruby_version_is "1.9" do
    describe "Rational when passed Integer" do
      it "returns a new Rational number with 1 as the denominator" do
        Rational(1).should eql(Rational(1, 1))
        Rational(-3).should eql(Rational(-3, 1))
        Rational(bignum_value).should eql(Rational(bignum_value, 1))
      end
    end
  end

  describe "Rational when passed Integer and Rational::Unify is defined" do
    after :each do
      Rational.send :remove_const, :Unify
    end

    it "returns the passed Integer when Rational::Unify is defined" do
      Rational::Unify = true

      Rational(1).should eql(1)
      Rational(-3).should eql(-3)
      Rational(bignum_value).should eql(bignum_value)
    end
  end
end
