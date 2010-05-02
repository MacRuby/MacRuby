# MacRuby implementation of IRB.
#
# This file is covered by the Ruby license. See COPYING for more details.
# 
# Copyright (C) 2009-2010, Eloy Duran <eloy.de.enige@gmail.com>

require 'irb/context'
require 'irb/source'
require 'irb/version'

require 'irb/ext/history'

if !ENV['SPECCING'] && defined?(RUBY_ENGINE) && RUBY_ENGINE == "macruby"
  require 'irb/ext/macruby'
end
