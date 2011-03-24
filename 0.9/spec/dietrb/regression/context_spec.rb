require File.expand_path('../../spec_helper', __FILE__)

main = self

describe "IRB::Context, when evaluating source" do
  before do
    @context = IRB::Context.new(main)
    def @context.printed;      @printed ||= ''          end
    def @context.puts(string); printed << "#{string}\n" end
  end
  
  it "does not assign the result to the `_' variable in one go, so it doesn't show up in a syntax error" do
    @context.evaluate("'banana;")
    @context.printed.should_not include("_ = ('banana;)")
  end
end