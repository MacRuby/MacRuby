# MacRuby implementation of IRB.
#
# This file is covered by the Ruby license. See COPYING for more details.
# 
# Copyright (C) 2009-2010, Eloy Duran <eloy.de.enige@gmail.com>

module IRB
  module VERSION #:nodoc:
    MAJOR = 0
    MINOR = 4
    TINY  = 5
    
    STRING = [MAJOR, MINOR, TINY].join('.')
    DESCRIPTION = "#{STRING} (DietRB)"
  end
end