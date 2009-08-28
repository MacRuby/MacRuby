require File.dirname(__FILE__) + '/../../spec_helper'

ruby_version_is "1.9" do
  describe "Random#int" do

    it "accepts an Integer argument" do
      lambda do
        Random.new.int(20)
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a Bignum argument" do
      lambda do
        Random.new.int(bignum_value)
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a Range argument" do
      lambda do
        Random.new.int(1..3)
      end.should_not raise_error(ArgumentError)
    end

    it "coerces the argument with #to_int" do
      obj = mock_numeric('int')
      obj.should_receive(:to_int).exactly(20).times.and_return(4)
      prng = Random.new
      ints = 20.times.map { prng.int(obj) }
      ints.max.should < 4
    end

    it "returns an Integer" do
      Random.new.int(12).should be_kind_of(Integer)
    end

    it "returns an number >= 0" do
      prng = Random.new
      ints = 20.times.map { prng.int(10) }
      ints.min.should >= 0
    end

    it "returns a number < the argument" do
      prng = Random.new
      ints = 20.times.map { prng.int(10) }
      ints.max.should < 10
    end

    it "returns a random number on each call that satisfies the given inequalities" do
      prng_a = Random.new
      ints_a = 20.times.map { prng_a.int(5) }

      prng_b = Random.new
      ints_b = 20.times.map { prng_b.int(5) }

      ints_a.should_not == ints_b
    end

    it "returns the same sequence of numbers for a given seed" do
      prng_a = Random.new(34)
      ints_a = 20.times.map { prng_a.int(5) }

      prng_b = Random.new(34)
      ints_b = 20.times.map { prng_b.int(5) }

      ints_a.should == ints_b
    end

    it "returns an integer from the inclusive Integer range if one is given" do
      prng = Random.new
      ints = 20.times.map { prng.int(10..12) }
      ints.uniq.sort.should == [10,11,12]
    end

    it "returns an integer from the exclusive Integer range if one is given" do
      prng = Random.new
      ints = 20.times.map { prng.int(10...12) }
      ints.uniq.sort.should == [10,11]
    end

    it "raises a TypeError if a String range is given" do
      lambda do
        Random.new.int('10'...'12')
      end.should raise_error(TypeError)
    end

    # (#int(3.3..5) previously segfaulted (bug #1859) which is why we test it
    # here. Now, #int(3.3..5) is interpreted as #int(3...5), which has been
    # reported as part of the same bug
    it "converts each endpoint of the supplied Range to Integers" do
      prng = Random.new(272726)
      ints_a = 10.times.map { prng.int(3.3..5) }
      prng = Random.new(272726)
      ints_b = 10.times.map { prng.int(3..5) }
      ints_a.should == ints_b
    end

    # The following examples fail. This has been reported as bug #1858
    it "returns an ArgumentError when the argument is 1" do
      lambda do
        Random.new.int(1)
      end.should raise_error(ArgumentError)
    end

    it "returns an ArgumentError when the argument is 0" do
      lambda do
        Random.new.int(0)
      end.should raise_error(ArgumentError)
    end

    it "returns an ArgumentError when the argument is negative" do
      lambda do
        Random.new.int(-17)
      end.should raise_error(ArgumentError)
    end
  end
end
