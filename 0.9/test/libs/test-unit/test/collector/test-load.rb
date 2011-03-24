require 'tmpdir'
require 'pathname'

require 'test/unit'
require 'test/unit/collector/load'

class TestUnitCollectorLoad < Test::Unit::TestCase
  def setup
    @previous_descendants = Test::Unit::TestCase::DESCENDANTS.dup
    Test::Unit::TestCase::DESCENDANTS.clear

    @temporary_test_cases_module_name = "TempTestCases"
    ::Object.const_set(@temporary_test_cases_module_name, Module.new)

    @test_dir = Pathname(Dir.tmpdir) + "test-unit"
    @test_dir.rmtree if @test_dir.exist?
    @test_dir.mkpath
  end

  setup
  def setup_top_level_test_cases
    @test_case1 = @test_dir + "test_case1.rb"
    @test_case2 = @test_dir + "test_case2.rb"
    @no_load_test_case3 = @test_dir + "case3.rb"

    @test_case1.open("w") do |test_case|
      test_case.puts(<<-EOT)
module #{@temporary_test_cases_module_name}
  class TestCase1 < Test::Unit::TestCase
    def test1_1
    end

    def test1_2
    end
  end
end
EOT
    end

    @test_case2.open("w") do |test_case|
      test_case.puts(<<-EOT)
module #{@temporary_test_cases_module_name}
  class TestCase2 < Test::Unit::TestCase
    def test2
    end
  end
end
EOT
    end

    @no_load_test_case3.open("w") do |test_case|
      test_case.puts(<<-EOT)
module #{@temporary_test_cases_module_name}
  class NoLoadTestCase3 < Test::Unit::TestCase
    def test3
    end
  end
end
EOT
    end
  end

  setup
  def setup_sub_level_test_cases
    @sub_test_dir = @test_dir + "sub"
    @sub_test_dir.mkpath

    @sub_test_case4 = @sub_test_dir + "test_case4.rb"
    @no_load_sub_test_case5 = @sub_test_dir + "case5.rb"
    @sub_test_case6 = @sub_test_dir + "test_case6.rb"

    @sub_test_case4.open("w") do |test_case|
      test_case.puts(<<-EOT)
module #{@temporary_test_cases_module_name}
  class SubTestCase4 < Test::Unit::TestCase
    def test4_1
    end

    def test4_2
    end
  end
end
EOT
    end

    @no_load_sub_test_case5.open("w") do |test_case|
      test_case.puts(<<-EOT)
module #{@temporary_test_cases_module_name}
  class NoLoadSubTestCase5 < Test::Unit::TestCase
    def test5_1
    end

    def test5_2
    end
  end
end
EOT
    end

    @sub_test_case6.open("w") do |test_case|
      test_case.puts(<<-EOT)
module #{@temporary_test_cases_module_name}
  class SubTestCase6 < Test::Unit::TestCase
    def test6
    end
  end
end
EOT
    end
  end

  setup
  def setup_sub_level_test_cases2
    @sub2_test_dir = @test_dir + "sub2"
    @sub2_test_dir.mkpath

    @no_load_sub2_test_case7 = @sub2_test_dir + "case7.rb"
    @sub2_test_case8 = @sub2_test_dir + "test_case8.rb"
    @sub2_test_case9 = @sub2_test_dir + "test_case9.rb"

    @no_load_sub2_test_case7.open("w") do |test_case|
      test_case.puts(<<-EOT)
module #{@temporary_test_cases_module_name}
  class NoLoadSub2TestCase7 < Test::Unit::TestCase
    def test7_1
    end

    def test7_2
    end
  end
end
EOT
    end

    @sub2_test_case8.open("w") do |test_case|
      test_case.puts(<<-EOT)
module #{@temporary_test_cases_module_name}
  class Sub2TestCase8 < Test::Unit::TestCase
    def test8_1
    end

    def test8_2
    end
  end
end
EOT
    end

    @sub2_test_case9.open("w") do |test_case|
      test_case.puts(<<-EOT)
module #{@temporary_test_cases_module_name}
  class Sub2TestCase9 < Test::Unit::TestCase
    def test9
    end
  end
end
EOT
    end
  end

  setup
  def setup_svn_test_cases
    @svn_test_dir = @test_dir + ".svn"
    @svn_test_dir.mkpath

    @svn_test_case10 = @svn_test_dir + "test_case10.rb"

    @svn_test_case10.open("w") do |test_case|
      test_case.puts(<<-EOT)
module #{@temporary_test_cases_module_name}
  class SvnTestCase10 < Test::Unit::TestCase
    def test7
    end
  end
end
EOT
    end
  end

  setup
  def setup_sub_cvs_test_cases
    @sub_cvs_test_dir = @sub_test_dir + "CVS"
    @sub_cvs_test_dir.mkpath

    @sub_cvs_test_case11 = @sub_cvs_test_dir + "test_case11.rb"

    @sub_cvs_test_case11.open("w") do |test_case|
      test_case.puts(<<-EOT)
module #{@temporary_test_cases_module_name}
  class SubCVSTestCase11 < Test::Unit::TestCase
    def test11
    end
  end
end
EOT
    end
  end

  def teardown
    @test_dir.rmtree if @test_dir.exist?
    ::Object.send(:remove_const, @temporary_test_cases_module_name)
    Test::Unit::TestCase::DESCENDANTS.replace(@previous_descendants)
  end

  def test_simple_collect
    assert_collect([:suite, {:name => @sub_test_dir.basename.to_s},
                    [:suite, {:name => _test_case_name("SubTestCase4")},
                     [:test, {:name => "test4_1"}],
                     [:test, {:name => "test4_2"}]],
                    [:suite, {:name => _test_case_name("SubTestCase6")},
                     [:test, {:name => "test6"}]]],
                   @sub_test_dir.to_s)
  end

  def test_multilevel_collect
    assert_collect([:suite, {:name => "."},
                    [:suite, {:name => _test_case_name("TestCase1")},
                     [:test, {:name => "test1_1"}],
                     [:test, {:name => "test1_2"}]],
                    [:suite, {:name => _test_case_name("TestCase2")},
                     [:test, {:name => "test2"}]],
                    [:suite, {:name => @sub_test_dir.basename.to_s},
                     [:suite, {:name => _test_case_name("SubTestCase4")},
                      [:test, {:name => "test4_1"}],
                      [:test, {:name => "test4_2"}]],
                     [:suite, {:name => _test_case_name("SubTestCase6")},
                      [:test, {:name => "test6"}]]],
                   [:suite, {:name => @sub2_test_dir.basename.to_s},
                     [:suite, {:name => _test_case_name("Sub2TestCase8")},
                      [:test, {:name => "test8_1"}],
                      [:test, {:name => "test8_2"}]],
                     [:suite, {:name => _test_case_name("Sub2TestCase9")},
                      [:test, {:name => "test9"}]]]])
  end

  def test_collect_file
    assert_collect([:suite, {:name => @test_case1.basename.to_s},
                    [:suite, {:name => _test_case_name("TestCase1")},
                     [:test, {:name => "test1_1"}],
                     [:test, {:name => "test1_2"}]]],
                   @test_case1.to_s)

    assert_collect(nil, @no_load_sub_test_case5.to_s)
  end

  def test_nil_pattern
    assert_collect([:suite, {:name => @sub_test_dir.basename.to_s},
                    [:suite, {:name => _test_case_name("NoLoadSubTestCase5")},
                     [:test, {:name => "test5_1"}],
                     [:test, {:name => "test5_2"}]],
                    [:suite, {:name => _test_case_name("SubTestCase4")},
                     [:test, {:name => "test4_1"}],
                     [:test, {:name => "test4_2"}]],
                    [:suite, {:name => _test_case_name("SubTestCase6")},
                     [:test, {:name => "test6"}]]],
                   @sub_test_dir.to_s) do |collector|
      collector.patterns.clear
    end
  end

  def test_filtering
    assert_collect([:suite, {:name => "."},
                    [:suite, {:name => _test_case_name("TestCase1")},
                     [:test, {:name => "test1_1"}],
                     [:test, {:name => "test1_2"}]]]) do |collector|
      collector.filter = Proc.new do |test|
        !/\Atest1/.match(test.method_name).nil?
      end
    end
  end

  def test_collect_multi
    test_dirs = [@sub_test_dir.to_s, @sub2_test_dir.to_s]
    assert_collect([:suite, {:name => "[#{test_dirs.join(', ')}]"},
                    [:suite, {:name => @sub_test_dir.basename.to_s},
                     [:suite, {:name => _test_case_name("SubTestCase4")},
                      [:test, {:name => "test4_1"}],
                      [:test, {:name => "test4_2"}]],
                     [:suite, {:name => _test_case_name("SubTestCase6")},
                      [:test, {:name => "test6"}]]],
                   [:suite, {:name => @sub2_test_dir.basename.to_s},
                     [:suite, {:name => _test_case_name("Sub2TestCase8")},
                      [:test, {:name => "test8_1"}],
                      [:test, {:name => "test8_2"}]],
                     [:suite, {:name => _test_case_name("Sub2TestCase9")},
                      [:test, {:name => "test9"}]]]],
                   *test_dirs)
  end

  private
  def assert_collect(expected, *collect_args)
    keep_required_files do
      Dir.chdir(@test_dir.to_s) do
        collector = Test::Unit::Collector::Load.new
        yield(collector) if block_given?
        actual = inspect_test_object(collector.send(:collect, *collect_args))
        assert_equal(expected, actual)
      end
    end
  end

  def keep_required_files
    required_files = $".dup
    yield
  ensure
    $".replace(required_files)
  end

  def _test_case_name(test_case_class_name)
    "#{@temporary_test_cases_module_name}::#{test_case_class_name}"
  end

  def inspect_test_object(test_object)
    return nil if test_object.nil?
    case test_object
    when Test::Unit::TestSuite
      sub_tests = test_object.tests.collect do |test|
        inspect_test_object(test)
      end.sort_by do |type, attributes, *children|
        attributes[:name]
      end
      [:suite, {:name => test_object.name}, *sub_tests]
    when Test::Unit::TestCase
      [:test, {:name => test_object.method_name}]
    else
      raise "unexpected test object: #{test_object.inspect}"
    end
  end
end
