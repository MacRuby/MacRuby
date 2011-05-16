require File.expand_path('../../../spec_helper', __FILE__)
require File.expand_path('../fixtures/classes', __FILE__)

describe "IO#printf" do
  before :each do
    @name = tmp("io_printf.txt")
    @io = new_io @name
    @io.sync = true
  end

  after :each do
    @io.close unless @io.closed?
    rm_r @name
  end

  it "writes the #sprintf formatted string" do
    @io.printf "%d %s", 5, "cookies"
    @name.should have_data("5 cookies")
  end

  it "raises IOError on closed stream" do
    lambda { IOSpecs.closed_io.printf("stuff") }.should raise_error(IOError)
  end
end
