require File.dirname(__FILE__) + '/../../spec_helper'

ruby_version_is "1.9" do
  describe "Random.rand" do
    it "returns a Float if no max argument is passed" do
      Random.rand.should be_kind_of(Float)
    end

    it "returns a Float >= 0 if no max argument is passed" do
      floats = 200.times.map { Random.rand }
      floats.min.should >= 0
    end

    it "returns a Float < 1 if no max argument is passed" do
      floats = 200.times.map { Random.rand }
      floats.max.should < 1
    end

    it "returns the same sequence for a given seed if no max argument is passed" do
      Random.srand 33
      floats_a = 20.times.map { Random.rand }
      Random.srand 33
      floats_b = 20.times.map { Random.rand }
      floats_a.should == floats_b
    end

    it "returns a Integer if an Integer argument is passed" do
      Random.rand(20).should be_kind_of(Integer)
    end

    it "returns an Integer >= 0 if an Integer argument is passed" do
      ints = 200.times.map { Random.rand(34) }
      ints.min.should >= 0
    end

    it "returns an Integer < the max argument if an Integer argument is passed" do
      ints = 200.times.map { Random.rand(55) }
      ints.max.should < 55
    end

    it "returns the same sequence for a given seed if an Integer argument is passed" do
      Random.srand 33
      floats_a = 20.times.map { Random.rand(90) }
      Random.srand 33
      floats_b = 20.times.map { Random.rand(90) }
      floats_a.should == floats_b
    end

    it "coerces arguments to Integers with #to_int" do
      obj = mock_numeric('int')
      obj.should_receive(:to_int).and_return(99)
      Random.rand(obj).should be_kind_of(Integer)
    end
  end
end
