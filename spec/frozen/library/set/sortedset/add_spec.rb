require File.dirname(__FILE__) + '/../../../spec_helper'
require 'set'
require File.dirname(__FILE__) + '/shared/add'

describe "SortedSet#add" do
  it_behaves_like :sorted_set_add, :add

  ruby_bug "redmine #118", "1.9" do
    it "takes only comparable values" do
      lambda { SortedSet[3, 4].add(SortedSet[5, 6]) }.should raise_error(ArgumentError)
    end
  end
end

describe "SortedSet#add?" do
  before :each do
    @set = SortedSet.new
  end

  it "adds the passed Object to self" do
    @set.add?("cat")
    @set.should include("cat")
  end

  it "returns self when the Object has not yet been added to self" do
    @set.add?("cat").should equal(@set)
  end

  it "returns nil when the Object has already been added to self" do
    @set.add?("cat")
    @set.add?("cat").should be_nil
  end
end
