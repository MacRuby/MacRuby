require File.dirname(__FILE__) + '/../../spec_helper'

describe "Hash.[]" do
  it "creates a Hash; values can be provided as the argument list" do
    Hash[:a, 1, :b, 2].should == {:a => 1, :b => 2}
    Hash[].should == {}
    Hash[:a, 1, :b, {:c => 2}].should == {:a => 1, :b => {:c => 2}}
  end

  it "creates a Hash; values can be provided as one single hash" do
    Hash[:a => 1, :b => 2].should == {:a => 1, :b => 2}
    Hash[{1 => 2, 3 => 4}].should == {1 => 2, 3 => 4}
    Hash[{}].should == {}
  end

  it "raises an ArgumentError when passed an odd number of arguments" do
    lambda { Hash[1, 2, 3] }.should raise_error(ArgumentError)
    lambda { Hash[1, 2, {3 => 4}] }.should raise_error(ArgumentError)
  end

  ruby_bug "#", "1.8.6" do
    it "call to_hash" do
      obj = mock('x')
      def obj.to_hash() { 1 => 2, 3 => 4 } end
      Hash[obj].should == { 1 => 2, 3 => 4 }
    end
  end

  it "returns an instance of the class it's called on" do
    Hash[MyHash[1, 2]].class.should == Hash
    MyHash[Hash[1, 2]].class.should == MyHash
  end
end
