describe "Block parameters" do
  it "does not override a shadowed variable from the outer scope" do
    i = 0
    a = [1,2,3]
    a.each {|i| ;}
    i.should == 0
  end

  it "captures variables from the outer scope" do
    a = [1,2,3]
    sum = 0
    var = nil
    a.each {|var| sum += var}
    sum.should == 6
    var.should == nil
  end
end

describe "Block parameters (to be removed from MRI)" do
  it "raises a SyntaxError when using a global variable" do
    lambda do
      instance_eval "[].each {|$global_for_block_assignment| ;}"
    end.should raise_error(SyntaxError)
  end
  
  it "raises a SyntaxError when making a method call" do
    lambda do
      instance_eval "o = Object.new; [].each {|o.id| }"
    end.should raise_error(SyntaxError)
  end
end