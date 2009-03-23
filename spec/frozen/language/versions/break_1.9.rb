describe "The break statement" do
  it "raises a SyntaxError if used not within block or while/for loop" do
    lambda { eval "def x; break; end" }.should raise_error(SyntaxError)
  end
end