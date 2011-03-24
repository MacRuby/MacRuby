module Test
  module Unit
    AutoRunner.register_runner(:tap) do |auto_runner|
      require 'test/unit/ui/tap/testrunner'
      Test::Unit::UI::Tap::TestRunner
    end
  end
end
