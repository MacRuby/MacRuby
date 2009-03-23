describe "The unless expression" do
  it "raises a SyntaxError when expression and body are on one line (using ':')" do
    lambda { instance_eval "unless false: 'foo'; end" }.should raise_error(SyntaxError)
  end
end