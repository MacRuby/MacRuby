describe "An anonymous module" do
  it "returns an empty string as its name" do
    m = Module.new
    m.name.should == ""
  end
end