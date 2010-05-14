describe :language_pseudo_variable, :shared => true do
  before do
    @name = @method
    @assigned_expression = @object
  end

  it "is syntactically inassignable" do
    lambda {
      eval("#{@name} = #{@assigned_expression}")
    }.should raise_error(SyntaxError)
  end
end

