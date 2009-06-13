# TODO
# require File.expand_path('../spec_helper', __FILE__)
# require File.expand_path('../spec_helper.rb', __FILE__)

require File.dirname(__FILE__) + "/../spec_helper"

describe "File#expand_path" do
  it "removes prefixed slashes, which isn't done by MRI" do
    File.expand_path('//').should == '/'
    File.expand_path('////').should == '/'
    File.expand_path('////foo').should == '/foo'
    File.expand_path('////foo//bar').should == '/foo/bar'
  end
end