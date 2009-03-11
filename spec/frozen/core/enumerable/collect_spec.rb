require File.dirname(__FILE__) + '/../../spec_helper'
require File.dirname(__FILE__) + '/fixtures/classes'
require File.dirname(__FILE__) + '/shared/collect'

describe "Enumerable#collect" do   
  it_behaves_like(:enumerable_collect , :collect)

  ruby_version_is "" ... "1.9" do
    it "gathers whole arrays as elements when each yields multiple" do
      multi = EnumerableSpecs::YieldsMulti.new
      multi.collect {|e| e}.should == [[1,2],[3,4,5],[6,7,8,9]]
    end
  end

  ruby_version_is "1.9" do
    it "gathers initial args as elements when each yields multiple" do
      multi = EnumerableSpecs::YieldsMulti.new
      multi.collect {|e| e}.should == [1,3,6]
    end
  end
end
