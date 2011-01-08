# $RoughId: extconf.rb,v 1.6 2001/07/13 15:38:27 knu Exp $
# $Id: extconf.rb 12501 2007-06-10 03:06:15Z nobu $

require "mkmf"

$INCFLAGS << ' -I../..'

create_makefile("digest")
