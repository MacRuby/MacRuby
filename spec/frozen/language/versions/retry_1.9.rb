describe "The retry statement" do
  it "raises a SyntaxError if used outside of a block" do
    lambda { eval "def bad_meth_retry; retry; end" }.should raise_error(SyntaxError)
    lambda { eval "lambda { retry }.call"          }.should raise_error(SyntaxError)
  end
end