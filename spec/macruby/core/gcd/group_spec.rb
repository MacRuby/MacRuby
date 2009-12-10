require File.dirname(__FILE__) + "/../../spec_helper"

if MACOSX_VERSION >= 10.6  
  describe "Dispatch::Group" do
    it "returns an instance of Group" do
      @group = Dispatch::Group.new
      @group.should be_kind_of(Dispatch::Group)
    end
    
    describe "#notify" do
    end
    
    describe "#wait" do
    end
  end
end
