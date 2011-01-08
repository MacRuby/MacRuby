require 'mkmf'

$defs << "-DHAVE_CONFIG_H"
$INCFLAGS << " -I$(srcdir)/.. -I../../.."

create_makefile('digest/bubblebabble')
