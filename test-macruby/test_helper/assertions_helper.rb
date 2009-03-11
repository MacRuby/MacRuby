require "test/unit"

module AssertionsExt
  # Asserts that at the end of the block the result of evaluating the given
  # +eval_string+ differs by +difference+.
  #
  #   a = []
  #
  #   assert_difference("a.length", +1) do
  #     a << "foo"
  #   end
  #
  #   assert_difference("a.length", -1) do
  #     a.pop
  #   end
  def assert_difference(eval_string, difference)
    before = instance_eval(eval_string)
    yield
    assert_equal (before + difference), instance_eval(eval_string)
  end
  
  # Asserts that at the end of the block the result of evaluating the given
  # +eval_string+ does _not_ differ.
  #
  #   a = Set.new
  #   a << "foo"
  #
  #   assert_no_difference("a.length") do
  #     a << "foo"
  #   end
  def assert_no_difference(eval_string, &block)
    assert_difference(eval_string, 0, &block)
  end
end

Test::Unit::TestCase.send(:include, AssertionsExt)