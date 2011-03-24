require 'mkmf'

$INCFLAGS << ' -I../..'

create_makefile('nkf')
