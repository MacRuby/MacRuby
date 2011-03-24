# MacRuby implementation of IRB.
#
# This file is covered by the Ruby license. See COPYING for more details.
# 
# Copyright (C) 2009-2010, Eloy Duran <eloy.de.enige@gmail.com>

require 'irb/context'
require 'irb/driver'
require 'irb/source'
require 'irb/version'

require 'irb/deprecated'

module IRB
  class << self
    # This is just here for so the ruby 1.9 IRB will seemingly work, but actually
    # loads DietRB, how cunning...
    #
    # This will obviously be removed once we've conquered the world.
    def start(*)
      warn "[!] Note that you are now actually using DietRB (#{IRB::VERSION::STRING})\n"
      load File.expand_path('../../bin/dietrb', __FILE__)
    end
    alias_method :setup, :start
  end
end
