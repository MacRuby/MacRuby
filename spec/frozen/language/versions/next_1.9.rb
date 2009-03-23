describe "The next statement" do
  it "raises a SyntaxError if used not within block or while/for loop" do
    lambda { eval "def bad_meth; next; end" }.should raise_error(SyntaxError)
  end
end