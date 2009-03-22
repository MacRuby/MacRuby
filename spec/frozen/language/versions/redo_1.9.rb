describe "The redo statement" do
  it "raises a SyntaxError if used not within block or while/for loop" do
    lambda do
      eval "def bad_meth_redo; redo; end"
    end.should raise_error(SyntaxError)
  end
end