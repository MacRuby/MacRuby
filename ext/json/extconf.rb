# encoding: UTF-8
require 'mkmf'
require 'rbconfig'

$INCFLAGS << ' -I../..'
$CFLAGS << ' -Wall -std=c99'

create_makefile("json")
