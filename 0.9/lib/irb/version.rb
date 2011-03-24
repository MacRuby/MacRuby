# MacRuby implementation of IRB.
#
# This file is covered by the Ruby license. See COPYING for more details.
# 
# Copyright (C) 2009-2010, Eloy Duran <eloy.de.enige@gmail.com>

module IRB
  module VERSION #:nodoc:
    NAME  = 'DietRB'
    MAJOR = 0
    MINOR = 6
    TINY  = 1
    
    STRING = [MAJOR, MINOR, TINY].join('.')
    DESCRIPTION = "#{NAME} (#{STRING})"
  end
  
  def self.version
    IRB::VERSION::DESCRIPTION
  end
end
