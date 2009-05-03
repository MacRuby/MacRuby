require 'enumerator'

describe :enumerator_enum_for, :shared => true do
  it "is defined in Kernel" do
    Kernel.method_defined?(@method).should be_true
  end

  it "returns a new enumerator" do
    "abc".send(@method).should be_kind_of(Enumerable::Enumerator)
  end

   it "defaults the first argument to :each" do
    enum = [1,2].send(@method, :each)
    enum.map { |v| v }.should == [1,2].each { |v| v }
  end
end
