require 'test/unit'

class TestUnitPriority < Test::Unit::TestCase
  class TestCase < Test::Unit::TestCase
    class << self
      def suite
        Test::Unit::TestSuite.new(name)
      end
    end

    priority :must
    def test_must
      assert(true)
    end

    def test_must_inherited
      assert(true)
    end

    priority :important
    def test_important
      assert(true)
    end

    def test_important_inherited
      assert(true)
    end

    priority :high
    def test_high
      assert(true)
    end

    def test_high_inherited
      assert(true)
    end

    priority :normal
    def test_normal
      assert(true)
    end

    def test_normal_inherited
      assert(true)
    end

    priority :low
    def test_low
      assert(true)
    end

    def test_low_inherited
      assert(true)
    end

    priority :never
    def test_never
      assert(true)
    end

    def test_never_inherited
      assert(true)
    end
  end

  def test_priority
    assert_priority("must", 1.0, 0.0001)
    assert_priority("important", 0.9, 0.09)
    assert_priority("high", 0.70, 0.1)
    assert_priority("normal", 0.5, 0.1)
    assert_priority("low", 0.25, 0.1)
    assert_priority("never", 0.0, 0.0001)
  end

  def assert_priority(priority, expected, delta)
    assert_need_to_run("test_#{priority}", expected, delta)
    assert_need_to_run("test_#{priority}_inherited", expected, delta)
  end

  def assert_need_to_run(test_name, expected, delta)
    test = TestCase.new(test_name)
    n = 1000
    n_need_to_run = 0
    n.times do |i|
      n_need_to_run +=1 if Test::Unit::Priority::Checker.need_to_run?(test)
    end
    assert_in_delta(expected, n_need_to_run.to_f / n, delta)
  end

  class SpecialNameTestCase < Test::Unit::TestCase
    class << self
      def suite
        Test::Unit::TestSuite.new(name)
      end
    end

    def test_question?
    end

    def test_exclamation!
    end

    def test_equal=
    end
  end

  def test_escaped?
    assert_escaped_name("test_question.predicate", "test_question?")
    assert_escaped_name("test_exclamation.destructive", "test_exclamation!")
    assert_escaped_name("test_equal.equal", "test_equal=")
  end

  def assert_escaped_name(expected, test_method_name)
    checker = Checker.new(SpecialNameTestCase.new(test_method_name))
    passed_file = checker.send(:passed_file)
    method_name_component = File.basename(File.dirname(passed_file))
    assert_equal(expected, method_name_component)
  end
end
