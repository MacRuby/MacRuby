assert ":ok", "File.open('#{__FILE__}', 'r') { p :ok }"

assert "true", "p(Dir['../*.c'].length > 1)"
assert "true", "p(Dir.glob('../*.c').length > 1)"
assert '', "#!ruby\n;" # fails because of a bug in ungetc that makes ruby read "!\n;"
assert '"abcdef"', %{
  f = File.open('#{__FILE__}')
  f.ungetc("\n")
  f.ungetc("f")
  f.ungetc("de")
  f.ungetc("c")
  f.ungetc("ab")
  p f.gets.strip
}

assert Process.uid, "p Process.uid"
assert Process.euid, "p Process.euid"

assert ":ok", "p File.expand_path('../fixtures/foo', '#{__FILE__}').include?('..') ? :fail : :ok"
assert ":ok", "require File.expand_path('../fixtures/foo', '#{__FILE__}')"