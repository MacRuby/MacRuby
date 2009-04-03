assert ":ok", "begin; p @@foo; rescue NameError; p :ok; end"
assert "42",  "@@foo = 42; p @@foo"
