assert "42", "$foo = 42; alias $bar $foo; p $bar"
assert "nil", "alias $bar $foo; p $bar"

assert "42", "def foo; 42; end; alias :bar :foo; p bar"
