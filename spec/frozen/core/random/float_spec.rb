require File.dirname(__FILE__) + '/../../spec_helper'

ruby_version_is "1.9" do
  describe "Random#float" do
    
    it "returns a Float object" do
      Random.new.float.should be_an_instance_of(Float)
    end

    it "returns a number greater than or equal to 0.0" do
      Random.new.float.should >= 0.0
    end

    it "returns a number less than 1.0 by default" do
      Random.new.float.should < 1.0
    end

    it "returns a different number on each call" do
      prng = Random.new
      floats = 20.times.map{ prng.float }
      floats.uniq.size.should == 20
    end

    it "returns the same sequence of values for a given seed" do
      prng = Random.new(176)
      2.times.map{ prng.float }.should == [0.3574378113049097, 0.21260493792986923]
    end

    it "only returns numbers less than the given argument" do
      prng = Random.new
      floats = 20.times.map{ prng.float(0.5) }
      floats.max.should < 0.5
    end

    it "accepts an optional Integer argument" do
      lambda do
        Random.new.float(20)
      end.should_not raise_error(ArgumentError)
    end

    it "accepts an optional Float argument" do
      lambda do
        Random.new.float(20.3)
      end.should_not raise_error(ArgumentError)
    end

    it "accepts an optional Rational argument" do
      lambda do
        Random.new.float(Rational(267,20))
      end.should_not raise_error(ArgumentError)
    end

    it "accepts an optional Complex (w/ imaginary part) argument" do
      lambda do
        Random.new.float(Complex(20))
      end.should_not raise_error(ArgumentError)
    end

    it "raises Errno::EDOM for an argument of Infinity" do
      lambda do
        Random.new.float(infinity_value)
      end.should raise_error(Errno::EDOM)
    end

    it "raises Errno::EDOM for an argument of NaN" do
      lambda do
        Random.new.float(nan_value)
      end.should raise_error(Errno::EDOM)
    end

   it "raises a RangeError for a Complex (with imaginary part) argument" do
      lambda do
        Random.new.float(Complex(20,3))
      end.should raise_error(RangeError)
    end

    it "raises a TypeError for a non-Floatish argument" do
      lambda do
        Random.new.float("swim")
      end.should raise_error(TypeError)
    end

  end
end
