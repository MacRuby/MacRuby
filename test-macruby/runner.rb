Dir.glob(File.expand_path('../cases/*_test.rb', __FILE__)).each do |test|
  require test
end