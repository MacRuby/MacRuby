require 'test/unit'
framework 'Foundation'

# Add ruby lib to the load path
$:.unshift(File.expand_path('../../lib', __FILE__))

FIXTURE_PATH = File.expand_path('../fixtures', __FILE__)
TMP_PATH = File.expand_path('../tmp', __FILE__)

# Load all test helpers
Dir.glob(File.expand_path('../test_helper/*_helper.rb', __FILE__)).each do |helper|
  require helper
end
