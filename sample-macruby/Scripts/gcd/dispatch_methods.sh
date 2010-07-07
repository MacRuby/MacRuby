#!/bin/sh
DISPATCH=../../../lib/dispatch
grep "	" $DISPATCH/README.rdoc | sed "s/	//" | grep -v '\$ ' | tail +2
