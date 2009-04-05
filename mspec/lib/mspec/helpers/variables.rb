class Object
  # Convenience helper for casting variable names as
  # strings or symbols depending on if RUBY_VERSION
  # is lower than 1.9. Before Ruby 1.9 all variable
  # methods return arrays of symbols. However, since
  # Ruby 1.9 these methods return arrays of symbols.
  #
  # Example:
  #
  # describe "This" do
  #   before do
  #     @instance = Object.new
  #     @instance.instance_variable_set("@foo", "foo")
  #     @instance.instance_variable_set("@bar", "bar")
  #   end
  #
  #   it "contains specific instance variables" do
  #     @instance.instance_variables.should == variables("@foo", "@bar")
  #   end
  # end
  def variables(*vars)
    RUBY_VERSION < '1.9' ? vars.map { |v| v.to_s } : vars.map { |v| v.to_sym }
  end
  alias_method :variable, :variables
end