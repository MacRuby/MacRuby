# MacRuby implementation of IRB.
#
# This file is covered by the Ruby license. See COPYING for more details.
# 
# Copyright (C) 2009-2010, Eloy Duran <eloy.de.enige@gmail.com>

framework 'AppKit'

module IRB
  class Context
    alias_method :_run, :run
    
    def run
      if NSApplication.sharedApplication.running?
        _run
      else
        Thread.new do
          _run
          NSApplication.sharedApplication.terminate(self)
        end
        NSApplication.sharedApplication.run
      end
    end
  end
end