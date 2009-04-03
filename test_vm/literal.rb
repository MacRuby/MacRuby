assert '""', "s=''; p s"
assert '"foo"', "s='foo'; p s"

assert "[]", "a=[]; p a"
assert "[1, 2, 3]", "a=[1,2,3]; p a"
assert 'nil', "a=[]; p a[42]"

assert "{}", "h={}; p h"
assert "3", "h={:un=>1,:deux=>2}; p h[:un]+h[:deux]"

assert '"foo246bar"', "p \"foo#{1+1}#{2+2}#{3+3}bar\""

assert ":ok", 'p :ok'
assert ":ok", 'p :"ok"'
assert ":ok", 'p :"#{:ok}"'
assert ":\"42\"", 'p :"#{40+2}"'
assert ":foo42", 'p :"foo#{40+2}"'
