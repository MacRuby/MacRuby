#!/usr/local/bin/macruby

require "test/unit"

module KnownBugs
  class TestYaml < Test::Unit::TestCase
    require "yaml"
    class IDontWantToCrash; end
    
    def test_load_non_native_classes
      data = YAML.dump(IDontWantToCrash.new)
      assert_nothing_raised { YAML.load(data) }
    end
  end
end