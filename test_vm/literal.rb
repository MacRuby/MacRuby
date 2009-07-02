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

assert 'true', "p :foo == :foo"
assert 'true', "p :foo === :foo"
assert 'true', "p :foo != :bar"
assert 'false', "p :foo == :bar"
assert 'false', "p :foo === :bar"
assert 'false', "p :foo != :foo"

assert '42', "class Symbol; def ==(o); p 42; end; end; :foo == :foo"
assert '42', "class Symbol; def ===(o); p 42; end; end; :foo === :foo"
assert '42', "class Symbol; def !=(o); p 42; end; end; :foo != :foo"

assert "false", "p ['foo'] == [:foo]"

assert '424242', "a=''; 3.times { b=''; b << '42'; a<<b }; p a.to_i"
