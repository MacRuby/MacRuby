assert "M", "module M; end; p M"
assert "Module", "module M; end; p M.class"

assert "42", "x = module Foo; 42; end; p x"
assert "nil", "x = module Foo; end; p x"
