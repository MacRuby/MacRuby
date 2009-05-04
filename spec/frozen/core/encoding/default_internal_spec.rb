require File.dirname(__FILE__) + '/../../spec_helper'

describe "Encoding.default_internal" do
  it "returns `nil' by default" do
    Encoding.default_internal.should be_nil
  end
end

describe "Encoding.default_internal=" do
  before :all do
    @before = Encoding.default_external
    Encoding.default_external = 'ISO-8859-1'
  end

  after :all do
    Encoding.default_external = @before
  end

  after :each do
    Encoding.default_internal = nil
  end

  it "takes an Encoding instance" do
    Encoding.default_internal = Encoding.find('UTF-8')
    Encoding.default_internal.name.should == 'UTF-8'

    Encoding.default_internal = Encoding.find('US-ASCII')
    Encoding.default_internal.name.should == 'US-ASCII'
  end

  it "takes a string name of an encoding" do
    Encoding.default_internal = 'UTF-8'
    Encoding.default_internal.name.should == 'UTF-8'

    Encoding.default_internal = 'US-ASCII'
    Encoding.default_internal.name.should == 'US-ASCII'
  end

  it "assigns the default external encoding to be used for IO" do
    Encoding.default_internal = 'UTF-8'
    open(__FILE__) do |file|
      file.internal_encoding.name.should == 'UTF-8'
    end

    Encoding.default_internal = 'US-ASCII'
    open(__FILE__) do |file|
      file.internal_encoding.name.should == 'US-ASCII'
    end
  end
end
