describe "The if expression" do
  it "raises a SyntaxError if a colon is used" do
    lambda { eval "if true: 1; end" }.should raise_error(SyntaxError)
  end
end