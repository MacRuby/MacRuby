path = File.dirname(__FILE__)
Dir.glob(File.join(path, 'test_*.rb')).each { |p| require(p) }
