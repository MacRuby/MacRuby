# TODO replace my with the regular runner.rb once we can run all tests!

$:.unshift('./lib')
require 'test/unit'

tests = %w{ array string stringchar hash objc }

tests.each { |x| require("test/ruby/test_#{x}.rb") }
