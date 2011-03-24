# $Id: test_scanfio.rb 25426 2009-10-21 03:44:56Z nobu $
#
# scanf for Ruby
#
# Ad hoc tests of IO#scanf (needs to be expanded)


require "scanf"

class TestScanfIO < Test::Unit::TestCase
  def test_io
    fh = File.new(File.join(File.dirname(__FILE__), "data.txt"), "r")
    assert_equal(0, fh.pos)
    assert_equal(["this", "is"], fh.scanf("%s%s"))
    assert_equal([33, "little"], fh.scanf("%da fun%s"))
  ensure
    fh.close
  end
end

