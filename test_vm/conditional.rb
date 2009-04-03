assert ':ok', "if true;  p :ok;  else; p :nok; end"
assert ':ok', "if false; p :nok; else; p :ok;  end"

assert ':ok', "if true;  p :ok;  end" 
assert '',    "if false; p :nok; end"

assert ':ok', "unless true;  p :nok; else; p :ok;  end"
assert ':ok', "unless false; p :ok;  else; p :nok; end"

assert ':ok', "if nil;     p :nok; else; p :ok;  end"
assert ':ok', "unless nil; p :ok;  else; p :nok; end"

assert ':ok', "if 42;     p :ok;  else; p :nok; end"
assert ':ok', "unless 42; p :nok; else; p :ok;  end"

assert '42', "x = if false; 43; else; 42; end; p x" 
assert '42', "x = if true;  42; else; 43; end; p x" 

assert ':ok', "if true and true; p :ok; else; p :nok; end"
assert ':ok', "if true and 42;   p :ok; else; p :nok; end"
assert ':ok', "if 42 and true;   p :ok; else; p :nok; end"
assert ':ok', "if 1 and 2 and 3; p :ok; else; p :nok; end"

assert ':ok', "if true and false;  p :nok; else; p :ok; end"
assert ':ok', "if false and true;  p :nok; else; p :ok; end"
assert ':ok', "if 1 and nil and 2; p :nok; else; p :ok; end"

assert ':ok', "if 1 or 2;             p :ok; else; p :nok; end"
assert ':ok', "if nil or 2;           p :ok; else; p :nok; end"
assert ':ok', "if 1 or false;         p :ok; else; p :nok; end"
assert ':ok', "if nil or 42 or false; p :ok; else; p :nok; end"

assert ':ok', "if nil or false; p :nok; else; p :ok; end"

assert 'false', 'p !true'
assert 'false', 'p (not true)'
assert 'true', 'p !false'
assert 'true', 'p (not false)'

assert '42', 'puts "4#{:dummy unless true}2"'

assert '42', "def foo; 42; end; def bar; p :nok; end; x = (foo || bar); p x"
assert ":ok\n42", "def foo; p :ok; nil; end; def bar; 42; end; x = (foo || bar); p x"
assert ":ok\n42", "def foo; p :ok; false; end; def bar; 42; end; x = (foo || bar); p x"
assert 'nil', "def foo; nil; end; def bar; nil; end; x = (foo || bar); p x"
assert 'false', "def foo; nil; end; def bar; false; end; x = (foo || bar); p x"

assert '42', "def foo; 123; end; def bar; 42; end; x = (foo && bar); p x"
assert 'nil', "def foo; nil; end; def bar; p :nok; end; x = (foo && bar); p x"
assert 'false', "def foo; false; end; def bar; p :nok; end; x = (foo && bar); p x"
assert ":ok\nnil", "def foo; p :ok; end; def bar; nil; end; x = (foo && bar); p x"
assert ":ok\nfalse", "def foo; p :ok; end; def bar; false; end; x = (foo && bar); p x"
