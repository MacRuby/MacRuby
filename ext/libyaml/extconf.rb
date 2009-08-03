require 'mkmf'

have_header("yaml.h")
have_header("yaml_private.h")
$INCFLAGS << ' -I../..'
create_makefile("libyaml")

