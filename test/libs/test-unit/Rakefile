# -*- ruby -*-

require 'rubygems'
gem 'rdoc'
require 'hoe'
require './lib/test/unit/version.rb'

ENV["NODOT"] = "yes"

version = Test::Unit::VERSION
ENV["VERSION"] = version
Hoe.spec('test-unit') do |p|
  Hoe::Test::SUPPORTED_TEST_FRAMEWORKS[:testunit2] = "test/run-test.rb"
  p.version = version
  p.developer('Kouhei Sutou', 'kou@cozmixng.org')
  p.developer('Ryan Davis', 'ryand-ruby@zenspider.com')

  # Ex-Parrot:
  # p.developer('Nathaniel Talbott', 'nathaniel@talbott.ws')
end

task :check_manifest => :clean_test_result
task :check_manifest => :clean_coverage

task :clean_test_result do
  test_results = Dir.glob("**/.test-result")
  sh("rm", "-rf", *test_results) unless test_results.empty?
end

task :clean_coverage do
  sh("rm", "-rf", "coverage")
end

task :tag do
  message = "Released Test::Unit #{version}!"
  base = "svn+ssh://#{ENV['USER']}@rubyforge.org/var/svn/test-unit/"
  sh 'svn', 'copy', '-m', message, "#{base}trunk", "#{base}tags/#{version}"
end

# vim: syntax=Ruby
