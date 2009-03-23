describe "An anonymous module" do
  it "has no name" do
    m = Module.new
    m.name.should == nil
  end
end