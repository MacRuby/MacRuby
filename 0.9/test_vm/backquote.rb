assert '"foo\\n"', 'p `echo foo`'
assert '"foo\\n"', 'def x; "foo"; end; p `echo #{x}`'
