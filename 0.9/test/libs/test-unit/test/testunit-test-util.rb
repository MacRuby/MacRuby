module TestUnitTestUtil
  private
  def assert_fault_messages(expected, faults)
    assert_equal(expected, faults.collect {|fault| fault.message})
  end

  def _run_test(test_case, name)
    result = Test::Unit::TestResult.new
    test = test_case.new(name)
    yield(test) if block_given?
    test.run(result) {}
    result
  end
end
