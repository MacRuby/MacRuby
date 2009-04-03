assert '42', "p  40 +  2"
assert '42', "p  44 + -2"
assert '42', "p  44 -  2"
assert '42', "p  40 - -2"
assert '42', "p  84 /  2"
assert '42', "p -84 / -2"
assert '42', "p  21 *  2"
assert '42', "p -21 * -2"

assert '42', "def x;  40; end; y =  2; p x + y"
assert '42', "def x;  44; end; y = -2; p x + y"
assert '42', "def x;  44; end; y =  2; p x - y"
assert '42', "def x;  40; end; y = -2; p x - y"
assert '42', "def x;  84; end; y =  2; p x / y"
assert '42', "def x; -84; end; y = -2; p x / y"
assert '42', "def x;  21; end; y =  2; p x * y"
assert '42', "def x; -21; end; y = -2; p x * y"

assert '42', %q{ 
  class Fixnum; def +(o); 42; end; end
  p 1+1
}
assert '42', %q{ 
  class Fixnum; def -(o); 42; end; end
  p 1-1
}
assert '42', %q{ 
  class Fixnum; def *(o); 42; end; end
  p 1*1
}
assert '42', %q{ 
  class Fixnum; def /(o); 42; end; end
  p 1/1
}

assert 'true',  "p 1 == 1"
assert 'false', "p 1 == 0"
assert 'true',  "p 1 != 0"
assert 'false', "p 1 == 0"
assert 'true',  "p 1  > 0"
assert 'false', "p 1  < 0"
assert 'true',  "p 1 >= 0"
assert 'false', "p 1  < 0"
assert 'true',  "p 0 >= 0"
assert 'false', "p 0  < 0"
assert 'true',  "p 0  < 1"
assert 'false', "p 0  > 1"
assert 'true',  "p 0 <= 1"
assert 'false', "p 0  > 1"
assert 'true',  "p 0 <= 0"
assert 'false', "p 0  > 0"

assert 'true',  "x = 1; y = 1; p x == y"
assert 'false', "x = 1; y = 0; p x == y"
assert 'true',  "x = 1; y = 0; p x != y"
assert 'false', "x = 1; y = 0; p x == y"
assert 'true',  "x = 1; y = 0; p x  > y"
assert 'false', "x = 1; y = 0; p x  < y"
assert 'true',  "x = 1; y = 0; p x >= y"
assert 'false', "x = 1; y = 0; p x  < y"
assert 'true',  "x = 0; y = 0; p x >= y"
assert 'false', "x = 0; y = 0; p x  < y"
assert 'true',  "x = 0; y = 1; p x  < y"
assert 'false', "x = 0; y = 1; p x  > y"
assert 'true',  "x = 0; y = 1; p x <= y"
assert 'false', "x = 0; y = 1; p x  > y"
assert 'true',  "x = 0; y = 0; p x <= y"
assert 'false', "x = 0; y = 0; p x  > y"

assert 'true', "p          42.class == Fixnum"
assert 'true', "p  1073741823.class == Fixnum", :archs => ['i386']
assert 'true', "p -1073741824.class == Fixnum", :archs => ['i386']
assert 'true', "x =  1073741823; x += 1; p x.class == Bignum", :archs => ['i386']
assert 'true', "x = -1073741824; x -= 1; p x.class == Bignum", :archs => ['i386']
assert 'true', "p  4611686018427387903.class == Fixnum", :archs => ['x86_64']
assert 'true', "p -4611686018427387904.class == Fixnum", :archs => ['x86_64']
assert 'true', "x =  4611686018427387903; x += 1; p x.class == Bignum", :archs => ['x86_64']
assert 'true', "x = -4611686018427387904; x -= 1; p x.class == Bignum", :archs => ['x86_64']

assert "6765\n75025\n832040", %q{
  def fib(n)
    if n < 2
      n
    else
      fib(n - 2) + fib(n - 1)
    end
  end
  p fib(20), fib(25), fib(30)
}

assert "40320\n362880\n3628800", %q{
  def fact(n)
    if n > 1
      n * fact(n - 1)
    else
      1
    end
  end
  p fact(8), fact(9), fact(10)
}
