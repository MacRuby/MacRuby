require File.dirname(__FILE__) + '/../../spec_helper'

describe "Encoding.default_external" do
  it "returns the default external Encoding instance" do
    Encoding.default_external.should be_kind_of(Encoding)
  end
end

describe "Encoding.default_external=" do
  before :all do
    @before = Encoding.default_external
  end

  after :each do
    Encoding.default_external = @before
  end

  it "takes an Encoding instance" do
    Encoding.default_external = Encoding.find('UTF-8')
    Encoding.default_external.name.should == 'UTF-8'

    Encoding.default_external = Encoding.find('US-ASCII')
    Encoding.default_external.name.should == 'US-ASCII'
  end

  it "takes a string name of an encoding" do
    Encoding.default_external = 'UTF-8'
    Encoding.default_external.name.should == 'UTF-8'

    Encoding.default_external = 'US-ASCII'
    Encoding.default_external.name.should == 'US-ASCII'
  end

  it "assigns the default external encoding to be used for IO" do
    Encoding.default_external = 'UTF-8'
    open(__FILE__) do |file|
      file.external_encoding.name.should == 'UTF-8'
    end

    Encoding.default_external = 'US-ASCII'
    open(__FILE__) do |file|
      file.external_encoding.name.should == 'US-ASCII'
    end
  end
end
