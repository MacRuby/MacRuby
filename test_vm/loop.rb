assert '10', %q{
  i = 0
  while i < 10
    i += 1
  end
  p i
}

assert '', "while false; p :nok; end"
assert '', "while nil;   p :nok; end"

assert ':ok', "x = 42; while x; p :ok; x = false; end" 
assert ':ok', "x = 42; while x; p :ok; x = nil;   end"

assert 'nil', "x = while nil; end; p x"

assert "42", "a=[20,10,5,5,1,1]; n = 0; for i in a; n+=i; end; p n"
assert "42", "a=[1,1,1,42]; for i in a; end; p i"
assert '42', %{
  def f(x)
    for y in 1..1
      p x
    end
  end
  f(42)
}

assert "42", "x = while true; break 42; end; p x"
assert "nil", "x = while true; break; end; p x"
assert "42", "i = 0; while i < 100; break if i == 42; i += 1; end; p i"

assert "42", "i = j = 0; while i < 100; i += 1; next if i > 42; j += 1; end; p j" 

assert "42", %q{
  i = 0; x = nil
  while i < 1
    x = 1.times { break 41 }
    i += 1
  end
  p x+i
}

assert "42", %q{
  x = nil
  1.times {
    x = while true; break 41; end
    x += 1
  }
  p x
}

assert 'nil', "x = until 123; 42; end; p x"
assert '42', 'x = nil; until x; x = 42; end; p x'
assert '42', "x = until nil; break 42; end; p x"
assert "nil", "x = until nil; break; end; p x"

assert "42", %q{
  foo = [42]
  until (x = foo.pop).nil?
    p x
  end
}
