require File.dirname(__FILE__) + '/../../spec_helper'
require 'tempfile'

describe "Tempfile#_close" do
  before(:each) do
    @tempfile = Tempfile.new("specs")
  end

  ruby_version_is "" ... "1.9" do  
    it "is protected" do
      @tempfile.protected_methods.should include("_close")
    end
  end

  ruby_version_is "1.9" do  
    it "is protected" do
      @tempfile.protected_methods.should include(:_close)
    end
  end
  
  it "closes self" do
    @tempfile.send(:_close)
    @tempfile.closed?.should be_true
  end
end
