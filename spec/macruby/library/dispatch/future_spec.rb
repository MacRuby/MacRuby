require File.dirname(__FILE__) + "/../../spec_helper"
require 'dispatch'

if MACOSX_VERSION >= 10.6
  describe "Dispatch::Future" do
    it "should do something" do
      true.should == true
    end
  end
end