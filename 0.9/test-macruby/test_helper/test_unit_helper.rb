require File.expand_path('../mini-test-spec', __FILE__)

class Test::Unit::TestCase
  # Returns the path to a file in +FIXTURE_PATH+.
  def fixture(name)
    File.join(FIXTURE_PATH, name)
  end
end