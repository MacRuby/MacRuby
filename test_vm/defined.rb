assert '"nil"',   "p defined? nil"
assert '"self"',  "p defined? self"
assert '"true"',  "p defined? true"
assert '"false"', "p defined? false"

assert '"expression"', "p defined? 123"
assert '"expression"', "p defined? 'foo'"
assert '"expression"', "p defined? [1,2,3]"
assert '"expression"', "p defined? []"

assert '"assignment"', "p defined? a=1"
assert '"assignment"', "p defined? $a=1"
assert '"assignment"', "p defined? @a=1"
assert '"assignment"', "p defined? A=1"
assert '"assignment"', "p defined? a||=1"
assert '"assignment"', "p defined? a&&=1"
assert '"assignment"', "1.times { |x| p defined? x=1 }"

assert '"local-variable"', "a = 123; p defined? a"
assert '"local-variable"', "1.times { |x| p defined? x }"

assert 'nil', "p defined? @a"
assert '"instance-variable"', "@a = 123; p defined? @a"

assert 'nil', "p defined? $a"
assert '"global-variable"', "$a = 123; p defined? $a"

assert 'nil', "p defined? A"
assert '"constant"', "A = 123; p defined? A"

assert 'nil', "p defined?(yield)"