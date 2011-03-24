class TestUnitColorScheme < Test::Unit::TestCase
  def test_default
    assert_equal({
                   "success" => color("green", :bold => true),
                   "failure" => color("red", :bold => true),
                   "pending" => color("magenta", :bold => true),
                   "omission" => color("blue", :bold => true),
                   "notification" => color("cyan", :bold => true),
                   "error" => color("yellow", :bold => true) +
                              color("black", :foreground => false),
                   "case" => color("white", :bold => true) +
                             color("blue", :foreground => false),
                   "suite" => color("white", :bold => true) +
                              color("green", :foreground => false),
                 },
                 Test::Unit::ColorScheme.default.to_hash)
  end

  def test_register
    inverted_scheme_spec = {
      "success" => {:name => "red"},
      "failure" => {:name => "green"},
    }
    Test::Unit::ColorScheme["inverted"] = inverted_scheme_spec
    assert_equal({
                   "success" => color("red"),
                   "failure" => color("green"),
                 },
                 Test::Unit::ColorScheme["inverted"].to_hash)
  end

  def test_new_with_colors
    scheme = Test::Unit::ColorScheme.new(:success => color("blue"),
                                         "failure" => color("green",
                                                            :underline => true))
    assert_equal({
                   "success" => color("blue"),
                   "failure" => color("green", :underline => true),
                 },
                 scheme.to_hash)
  end

  def test_new_with_spec
    scheme = Test::Unit::ColorScheme.new(:success => {
                                           :name => "blue",
                                           :bold => true
                                         },
                                         "failure" => {:name => "green"})
    assert_equal({
                   "success" => color("blue", :bold => true),
                   "failure" => color("green"),
                 },
                 scheme.to_hash)
  end

  private
  def color(name, options={})
    Test::Unit::Color.new(name, options)
  end
end
