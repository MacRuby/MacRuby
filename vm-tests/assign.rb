assert '42', "a,b,c = 40,1,1; p a+b+c"
assert '42', "@a,@b,@c = 40,1,1; p @a+@b+@c"
assert '42', "a,@b,c = 40,1,1; p a+@b+c" 

assert ':ok', %q{
  def foo; a,b,c=1,2,3; end
  p :ok if foo.is_a?(Array)
}

assert '42', "a = [30, 10,  2]; x,y,z = a; p x+y+z"
assert '42', "a = [30, 10, *2]; x,y,z = a; p x+y+z"

assert '42', "def foo=(x); @x=x; end; x,self.foo = 1,41; p @x+x"
assert '42', "def []=(x,y); @x=x+y; end; x,self[40] = 1,1; p @x+x"

assert '[1, 2, 3]', "a=[1,2,3]; x=*a; p x"
assert '[1, 2, 3]', "a=[2,3]; x=1,*a; p x"
assert '[1, 2, 3]', "a=[1,2]; x=*a,3; p x"
assert '[1, 2, 3]', "a=[2]; x=1,*a,3; p x"

assert ':ok', "a, b, c = 42; p :ok if a == 42 and b == nil and c == nil"
assert ':ok', "a, b, c = [1, 2, 3, 4]; p :ok if a == 1 and b == 2 and c == 3"
assert ':ok', "a, b, c = [1, 2]; p :ok if a == 1 and b == 2 and c == nil"
assert ':ok', "a, b, c = nil; p :ok if a == nil and b == nil and c == nil"

assert '[nil, [], nil]', "a, *b, c = nil; p [a, b, c]"
assert '[1, [], 2]', "a, *b, c = 1, 2; p [a, b, c]"
assert '[1, [2, 3], 4]', "a, *b, c = 1, 2, 3, 4; p [a, b, c]"
assert '[[1, 2, 3], 4]', "*a, b = 1, 2, 3, 4; p [a, b]"
assert '4', "*, a = 1, 2, 3, 4; p a"
assert '[1, [2, 3], 4]', "(a, *b), c = [1, 2, 3], 4; p [a, b, c]"

assert ':ok', '* = 1,2; p :ok'
assert '[1, 2]', 'x = (* = 1,2); p x'

assert '[42]', "a=[1,2,3]; b=[0,3]; a[*b]=42; p a"

assert '42', "a=[20]; a[0] += 22; p a[0]"
assert '42', "a=[80]; a[0] -= 38; p a[0]"
assert '42', "a=[84]; a[0] /= 2; p a[0]"
assert '42', "a=[21]; a[0] *= 2; p a[0]"

assert '42', "a=[]; a[0] ||= 42; p a[0]"
assert '42', "a=[42]; a[0] ||= 123; p a[0]"
assert '42', "a=[123]; a[0] &&= 42; p a[0]"
assert 'nil', "a=[]; a[0] &&= 123; p a[0]"

assert '42', %q{
  class Foo; attr_accessor :x; end
  o = Foo.new
  o.x ||= 42
  p o.x
}
assert '42', %q{
  class Foo; attr_accessor :x; end
  o = Foo.new
  o.x = 2
  o.x += 40
  p o.x
}

assert '42', "a ||= 42; p a"
assert '42', "a = nil;   a ||= 42; p a"
assert '42', "a = false; a ||= 42; p a"
assert '42', "a = 42; a ||= 40; p a"
assert '42', "a = 40; b = 2; c ||= a + b; p c"

assert '42', "@a ||= 42; p @a"
assert '42', "@a = nil;   @a ||= 42; p @a"
assert '42', "@a = false; @a ||= 42; p @a"
assert '42', "@a = 42; @a ||= 40; p @a"
assert '42', "@a = 40; @b = 2; @c ||= @a + @b; p @c"

assert 'nil', "a &&= 42; p a"
assert '42',  "a = 0; a &&= 42; p a"
assert ':ok', "a = nil; a &&= 42; p :ok if a == nil"
assert ':ok', "a = false; a &&= 42; p :ok if a == false"
assert '42',  "c = 123; a = 40; b = 2; c &&= a + b; p c"

assert 'nil', "@a &&= 42; p @a"
assert '42',  "@a = 0; @a &&= 42; p @a"
assert ':ok', "@a = nil; @a &&= 42; p :ok if @a == nil"
assert ':ok', "@a = false; @a &&= 42; p :ok if @a == false"
assert '42',  "@c = 123; @a = 40; @b = 2; @c &&= @a + @b; p @c"

assert ':ok', "x = ':  '; x[1,2] = 'ok'; puts x"

assert '42', "a=[4]; a += [2]; puts a.join"
assert '42', "a=[4,3,2]; a -= [3]; puts a.join"
