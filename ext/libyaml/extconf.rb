require 'mkmf'

have_header("yaml.h")
have_header("yaml_private.h")
$INCFLAGS << ' -I../..'
$CPPFLAGS << ' -g'
create_makefile("libyaml")

