#!/bin/sh
DISPATCH=../../../lib/dispatch
/bin/echo -n "#!"
which macruby
grep "	" $DISPATCH/README.rdoc | sed "s/	//" | grep -v '\$ '
