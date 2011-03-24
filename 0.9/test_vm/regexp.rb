assert "0",    "p /^abc/ =~ 'abcdef'"
assert "nil",  "p /^abc/ =~ 'abxyz'"
assert "/42/", "p /#{1+21+20}/"

assert ":ok", %q{
  def foo; "invalid["; end
  begin
    re = /#{foo}/
  rescue RegexpError
    p :ok
  end
}
