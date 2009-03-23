describe "The redo statement" do
  it "raises a SyntaxError if used not within block" do
    lambda do
      instance_eval "def bad_meth_redo; redo; end"
    end.should raise_error(SyntaxError)
  end
end