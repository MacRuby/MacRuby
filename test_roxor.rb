#!/usr/bin/ruby
# Preliminary test suite for the MacRuby VM.
# Aimed at testing critical features of the VM.
#
# Please do not contribute tests that cover any higher level features here, 
# use the rubyspec directory instead.

$test_only = []
test_commands = []
ARGV.each do |arg|
  if md = /--ruby=(.*)/.match(arg)
    test_commands << md[1]
  else
    $test_only << arg
  end
end
if test_commands.empty?
  miniruby_path = File.join(Dir.pwd, 'miniruby')
  test_commands << "arch -i386 #{miniruby_path}"
  test_commands << "arch -x86_64 #{miniruby_path}" if system("arch -x86_64 #{miniruby_path} -e '' 2> /dev/null")
end
$test_archs = {}
test_commands.each do |command|
  if md = /\barch -([^\s]+)/.match(command)
    arch_name = md[1]
  else
    arch_name = 'default'
  end
  $test_archs[arch_name] ||= []
  $test_archs[arch_name] << command
end
$problems = []
$assertions_count = 0

module Runner
  def self.assert(expectation, code, options={})
    return if options[:known_bug]
    if options[:archs]
      archs = $test_archs.select {|arch, command| options[:archs].include?(arch) }
    else
      archs = $test_archs
    end
    archs.each do |arch, commands|
      commands.each do |command|
        output = nil
        IO.popen(command, 'r+') do |io|
          io.puts(code)
          io.close_write
          output = io.read
        end
        result = if $? and $?.exitstatus == 0
          output.chomp == expectation ? '.' : 'F'
        else
          output = "ERROR CODE #{$?.exitstatus}"
          'E'
        end
        print result
        $stdout.flush
        if result != '.'
          $problems << [code, expectation, arch, command, output]
        end
        $assertions_count += 1
      end
    end
  end
end

def test(what, &p)
  if $test_only.empty? or $test_only.include?(what)
    print what
    print ' '  
    $stdout.flush
    Runner.module_eval(&p)
    puts ''
  end
end

at_exit do
  if $problems.empty?
    puts "Successfully passed all #{$assertions_count} assertions."
  else
    puts ''
    puts "#{$problems.size} assertion#{$problems.size > 1 ? 's' : ''} over #{$assertions_count} failed:"
    n = 0
    $problems.each do |code, expectation, arch, command, output|
      puts ''
      puts "Problem #{n += 1}:"
      puts "Code: #{code}"
      puts "Arch: #{arch}"
      puts "Command: #{command}"
      puts "Expectation: #{expectation}"
      puts "Output: #{output}"
    end
    exit 1
  end
end

=begin
Signal.trap("INT") do
  base = File.basename($test_ruby)
  puts "Sending SIGKILL to all #{base} instances..."
  system("killall -KILL #{base}")
end
=end

# TEST SUITE BEGINNING

test "conditionals" do

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

end

test "fixnums" do

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

end

test "literals" do

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

end

test "assign" do

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

end

test "constants" do

  assert '42', "FOO=42; p FOO"
  assert '42', "FOO=42; p Object::FOO"
  assert '42', "class X; FOO=42; end; p X::FOO"
  assert '42', "class X; end; X::FOO=42; p X::FOO"

  assert ':ok', %q{
    class X; FOO=123; end
    begin
      p FOO
    rescue NameError
      p :ok
    end
  }

  assert '42', "class X; FOO=42; def foo; FOO; end; end; p X.new.foo"
  assert '42', %q{
    FOO=123
    class X; FOO=42; end
    class Y < X; def foo; FOO; end; end
    p Y.new.foo
  }

  assert '42', "FOO=42; 1.times { p FOO }"
  assert '42', %q{
    module X
      FOO=42
      1.times { p FOO }
    end
  }

  assert '42', %q{
    class X
      FOO = 42
      class Y; def foo; FOO; end; end
    end
    p X::Y.new.foo
  }

  assert '42', %q{
    class X; FOO = 42; end
    class Y < X; def foo; FOO; end; end
    p Y.new.foo
  }

  assert '42', %q{
    class X; FOO = 123; end
    class Z
      FOO = 42
      class Y < X; def foo; FOO; end; end
    end
    p Z::Y.new.foo
  }

  assert '42', %q{
    class A
      module B
        def foo
          42
        end
      end
      module C
        extend B
      end
    end
    p A::C.foo
  }

  assert 'true', 'p ::String == String'

  assert '42', %q{
    o = Object.new
    class << o
      module Foo
        Bar = 42
      end
      class Baz; include Foo; end
      class Baz;
        def self.bar; Bar; end
      end
      def baz; Baz; end
    end
    p o.baz.bar
  }

  assert '42', %q{
    module M
      FOO = 42
      class X
        class << self; p FOO; end
      end
    end
  }
    
end

test "ranges" do

  assert '0..42', "r = 0..42; p r"
  assert '0..42', "b=0; e=42; r = b..e; p r"
  assert 'false', "r = 0..42; p r.exclude_end?"
  assert 'false', "b=0; e=42; r = b..e; p r.exclude_end?"

  assert '0...42', "r = 0...42; p r"
  assert '0...42', "b=0; e=42; r = b...e; p r"
  assert 'true',   "r = 0...42; p r.exclude_end?"
  assert 'true',   "b=0; e=42; r = b...e; p r.exclude_end?"

end

test "loops" do

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

end

test "class" do

  assert "X", "class X; end; p X"
  assert "Class", "class X; end; p X.class"
  assert "true", "class X; end; p X.superclass == Object"
  assert "Y", "class X; end; class Y < X; end; p Y"
  assert "X", "class X; end; class Y < X; end; p Y.superclass"

  assert "42", "class X; def initialize; p 42; end; end; X.new"
  assert "42", "class X; def initialize(x); p x; end; end; X.new(42)"

  assert "42", %q{
    class X
    end
    o = X.new
    class X
      def foo; p 42; end
    end
    o.foo
  }

  assert "42", %q{
    class X
      class << self
        def foo; 42; end
      end
    end
    p X.foo
  }

end

test "module" do

  assert "M", "module M; end; p M"
  assert "Module", "module M; end; p M.class"

end

test "attr" do

  assert '42', %q{
    class Foo; attr_accessor :foo; end
    o = Foo.new
    o.foo = 42
    p o.foo
  }
  
  assert '42', %q{
    class Foo
      attr_reader :foo
      attr_writer :foo
    end
    o = Foo.new
    o.foo = 42
    p o.foo
  }

  assert 'nil', "class Foo; attr_reader :foo; end; p Foo.new.foo"

  assert ':ok', %q{
    class Foo; attr_reader :foo; end
    o = Foo.new
    begin
      o.foo = 42
    rescue NoMethodError
      p :ok
    end
  }

  assert ':ok', %q{
    class Foo; attr_writer :foo; end
    o = Foo.new
    begin
      o.foo
    rescue NoMethodError
      p :ok
    end
  }

  assert '42', %q{
    class Foo
      attr_writer :foo
      def bar; @foo; end
    end
    o = Foo.new
    o.foo = 42
    p o.bar
  }

end

test "dispatch" do

  assert "42", "def foo; 42; end; p foo"
  assert "42", "def foo(x); x + 41; end; p foo(1)"
  assert "42", "def foo(x, y); x + y; end; p foo(40, 2)"
  assert "42", "def foo(x, y, z); x + y + z; end; p foo(40, 1, 1)"

  assert "42", "def foo(x=42); x; end; p foo"
  assert "42", "def foo(x=0); x; end; p foo(42)"
  assert "42", "def foo(x=30, y=10, z=2); x+y+z; end; p foo"
  assert "42", "def foo(x=123, y=456, z=789); x+y+z; end; p foo(30, 10, 2)"
  assert "42", "def foo(x, y=40); x+y; end; p foo(2)"
  assert "42", "def foo(x, y=123); x+y; end; p foo(20, 22)"
  assert "42", "def foo(x, y, z=2); x+y+z; end; p foo(20, 20)"
  assert "42", "def foo(x, y, z=123); x+y+z; end; p foo(20, 20, 2)"
  assert "42", "def foo(x, y=20, z=2); x+y+z; end; p foo(20)"
  assert "42", "def foo(x, y=123, z=2); x+y+z; end; p foo(30, 10)"

  assert "126", "def foo(a=b=c=42); a+b+c; end; p foo"
  assert "[42, nil]", "def f(a=X::x=b=1) p [a, b] end; f(42)"

  assert "42", "def foo; 1; end; i = 0; while i < 42; i += foo; end; p i"
  assert "42", %q{
    class X; def foo; 15; end; end
    class Y; def foo; 01; end; end
    class Z; def foo; 05; end; end
    x = X.new
    y = Y.new
    z = Z.new
    i = 0
    i += x.foo; i += y.foo; i += z.foo
    i += x.foo; i += y.foo; i += z.foo
    p i
  }

  assert "42", %q{
    class X;     def foo; p 42;  end; end
    class Y < X; def foo; super; end; end
    Y.new.foo
  }
  
  assert "42", %q{
    class X;     def foo(x); p x;       end; end
    class Y < X; def foo;    super(42); end; end
    Y.new.foo
  }

  assert "42", %q{
    class X;     def foo(x); p x;   end; end
    class Y < X; def foo(x); super; end; end
    Y.new.foo(42)
  }

  assert "42", %q{
    class X;     def foo; 42;    end; end
    class Y < X; def foo; super; end; end
    class Z < Y; def foo; super; end; end
    p Z.new.foo
  }

  assert "42", "def foo; 42; end; p send(:foo)"
  assert "42", "def foo(x, y); x + y; end; p send(:foo, 40, 2)"

  assert "42", %q{
    def foo; :nok; end
    def send(x); 42; end
    p send(:foo)
  }

  assert "42", %q{
    class Object
      def send(x); 42; end
    end
    def foo; :nok; end
    p send(:foo)
  }

  assert "42", "def foo; return 42; end; p foo"
  assert "42", "def foo(x); if x; return 42; end; end; p foo(true)"
  assert "42", "def foo(x); if x; x += 2; return x; end; end; p foo(40)"
  assert "42", "def foo(x); if x; x += 2; return x; x += 666; end; end; p foo(40)"

  assert "[1, 2, 3]", "def foo; return 1, 2, 3; end; p foo"

  assert "42", "def foo=(x); @x = x + 1; end; self.foo=41; p @x"
  assert "42", "def []=(x, y); @x = x + y; end; self[40]=2; p @x"

  assert "[]", "def foo; return *[]; end; p foo"

  assert '42', "def foo; 1.times { return 42 }; p :nok; end; p foo"

  assert "42", "def foo(x,y,z); x+y+z; end; a=[20,10,12]; p foo(*a)"
  assert "42", "def foo(x,y,z); x+y+z; end; a=[20,10]; p foo(*a, 12)"
  assert "42", "def foo(x,y,z); x+y+z; end; a=[20,10]; p foo(12, *a)"
  assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; p foo(10, 12, *a)"
  assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; p foo(10, *a, 12)"
  assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; p foo(*a, 10, 12)"
  assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; b=[10,12]; p foo(*a, *b)"
  assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; b=[10]; p foo(*a, 12, *b)"
  assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; b=[10]; p foo(12, *a, *b)"
  assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; b=[10]; p foo(*a, *b, 12)"
  assert "42", "def foo(x,y,z); x+y+z; end; a=[20]; b=[10]; c=[12]; p foo(*a, *b, *c)"
  assert "42", "def foo(x); x; end; a=42; p foo(*a)"

  assert ":ok", "def foo(*args); :ok; end; p foo"
  assert ":ok", "def foo(&block); :ok; end; p foo"
  assert ":ok", "def foo(*args, &block); :ok; end; p foo"
  assert ":ok", "def foo(x, *args, &block); x; end; p foo(:ok)"
  assert ":ok", "def f(&proc) p :ok; end; f(&nil)"
  
  assert ":ok", %{
    def foo(&block) p(block ? :ko : :ok) end
    def bar(&block) block.call() end
    bar { foo }
  }
  
  assert "[1, nil, :c]", "def f(a, b = :b, c = :c) [a, b, c] end; p f(1, nil)"
  assert "[1, :b, :c, 2]\n[1, 2, :c, 3]\n[1, 2, 3, 4]", %{
    def f(a, b = :b, c = :c, d) [a, b, c, d] end
    p f(1, 2)
    p f(1, 2, 3)
    p f(1, 2, 3, 4)
  }
  assert "[1, :b, :c, 2]\n[1, 2, :c, 3]\n[1, 2, 3, 4]", %{
    def f(a, b = :b, c = :c, d) [a, b, c, d] end
    p f(1, 2)
    p f(1, 2, 3)
    p f(1, 2, 3, 4)
  }
  assert "[1, :b, :c, [], 2, 3]\n[1, 2, :c, [], 3, 4]\n[1, 2, 3, [], 4, 5]\n[1, 2, 3, [4], 5, 6]\n[1, 2, 3, [4, 5], 6, 7]", %{
    def f(a, b = :b, c = :c, *args, d, e) [a, b, c, args, d, e] end
    p f(1, 2, 3)
    p f(1, 2, 3, 4)
    p f(1, 2, 3, 4, 5)
    p f(1, 2, 3, 4, 5, 6)
    p f(1, 2, 3, 4, 5, 6, 7)    
  }
  assert "42", "def f((a, b)); a end; p f([42, 53])"
  assert "42", "def f((a, b)); a end; p f([42, 53, 64])" # ignore additional elements in the array
  assert "[42, nil]", "def f((a, b)); [a, b] end; p f([42])" # not used args are set to nil
  assert "[1, 2, [], 3, nil]\n[1, 2, [], 3, 4]\n[1, 2, [3], 4, 5]", %{
    def f((x, y, *a, b, c)); [x, y, a, b, c] end
    p f([1, 2, 3])
    p f([1, 2, 3, 4])
    p f([1, 2, 3, 4, 5])
  }
  assert "true", %{
    class A; def to_ary; [42]; end; end
    def f((*a)); a; end;
    p f(A.new) == [42]
  } # to_ary (not to_a) is called on non-Array objects
  assert "true", %{def f((*a)); a; end; o = Object.new; p f(o) == [o]} # objects without to_ary are just passed in a one element array

  assert ":ok", "def f(x = 1) :ko; end; def f() end; begin p f(1); rescue ArgumentError; p :ok; end"
  assert ":ok", "def f(x) :ko; end; def f() end; begin p f(1); rescue ArgumentError; p :ok; end"
  assert ":ok", "def f() :ko; end; def f(x) end; begin p f(); rescue ArgumentError; p :ok; end"

  assert ":ok", "def f(); end; begin f(1); rescue ArgumentError; p :ok; rescue; p :ko; end"
  assert ":ok", "def f(a); end; begin f; rescue ArgumentError; p :ok; rescue; p :ko; end"
  assert ":ok", "def f(a); end; begin f(1, 2); rescue ArgumentError; p :ok; rescue; p :ko; end"
  assert ':ok', "def f(a, b); end; begin; f; rescue ArgumentError; p :ok; rescue; p :ko; end"
  assert ':ok', "def f(a, b); end; begin; f(1, 2, 3); rescue ArgumentError; p :ok; rescue; p :ko; end"
  
  assert ':ok', "def f(a, b); end; begin; a=[1]; f(*a); rescue ArgumentError; p :ok; rescue; p :ko; end"
  assert ':ok', "def f(a, b); end; begin; a=[1,2,3]; f(*a); rescue ArgumentError; p :ok; rescue; p :ko; end"

  assert ":ok", %{
    def func()
      1.times { |x| func() }
    end
    p :ok
  }

  assert "1\n2\n2", %q{
    def func
      p 1
      def func
        p 2
      end
      func
    end
    func
    func
  }
  
  assert "[1, 2]", %{
    def f
      yield 1, 2
    end
    f {|*args| p args}
  }

  assert ':ok', %{
    1.times do
      def foo(&a)
        a.call
      end
      foo { p :ok }
    end
  }

  assert '42', %{
    class Foo
      def self.foo; 42; end
    end
    p Foo.foo
  }
  assert '42', %{
    class Foo
      class << self
        def foo; 42; end
      end
    end
    p Foo.foo
  }
  assert '42', %{
    class Foo; end
    def Foo.foo; 42; end
    p Foo.foo
  }
  assert '42', %{
    o = Object.new
    def o.foo; 42; end
    p o.foo
  }
  assert '42', %{
    o = Object.new
    class << o
      def foo; 42; end
    end
    p o.foo
  }

  assert '42', %{
    def foo; p 42; end
    def bar(a = foo); end
    bar
  }

  assert '42', %{
    def foo() yield 1, 2 end
    x = 1
    w = 42
    foo { |x, y = :y| p w }
  }
end

test "blocks" do

  assert ":ok", "1.times { p :ok }"
  assert "42",  "p 42.times {}"
  assert "42",  "i = 0; 42.times { i += 1 }; p i"
  assert "42",  "x = nil; 43.times { |i| x = i }; p x"
  assert "",    "0.times { p :nok }"

  assert ":ok", "def foo; yield; end; foo { p :ok }"
  assert "42",  "def foo; yield 42; end; foo { |x| p x }"
  assert "",    "def foo; end; foo { p :nok }" 
  assert "42", %q{
    def foo; yield 20, 1, 20, 1; end
    foo do |a, b, c, d|
      x = a + b + c + d
      p x
    end
  }
  assert ":ok", %q{
    def foo; yield; end
    foo do |x, y, z| 
      p :ok if x == nil and y == nil and z == nil
    end
  }
  assert ":ok", %q{
    def foo; yield(1, 2); end
    foo do |x, y, z| 
      p :ok if x == 1 and y == 2 and z == nil
    end
  }
  assert ":ok", %q{
    def foo; yield(1, 2); end
    foo do |x| 
      p :ok if x == 1
    end
  }
  assert ":ok", %q{
    def foo; yield(1, 2); end
    foo do |x, y = :y, z|
      p :ok if x == 1 and y == :y and z == 2
    end
  }
  assert ":ok", %q{
    def foo; yield(1); end
    foo do |x, y = :y, z|
      p :ok if x == 1 and y == :y and z == nil
    end
  }
  assert ":ok", %q{
    def foo; yield(1, 2, 3, 4); end
    foo do |x, y = :y, *rest, z|
      p :ok if x == 1 and y == 2 and rest == [3] and z == 4
    end
  }
  assert ":ok", %q{
    def foo; yield([1, 2]); end
    foo do |x, y = :y, z|
      p :ok if x == 1 and y == :y and z == 2
    end
  }
  assert "[1, 2]", %q{
    def foo; yield(1, 2); end
    foo { |*rest| p rest }
  }
  assert "[[1, 2]]", %q{
    def foo; yield([1, 2]); end
    foo { |*rest| p rest }
  }
  assert "[1, [2]]", %q{
    def foo; yield([1, 2]); end
    foo { |a, *rest| p [a, rest] }
  }
  assert "[[1, 2], []]", %q{
    def foo; yield([1, 2]); end
    foo { |a = 42, *rest| p [a, rest] }
  }
  assert "[1, 2, []]", %q{
    def foo; yield([1, 2]); end
    foo { |a = 42, *rest, b| p [a, b, rest] }
  }
  assert "[1, 2, []]", %q{
    def foo; yield([1, 2]); end
    foo { |a, b = 42, *rest| p [a, b, rest] }
  }
  assert "[[1, 2], 42, []]", %q{
    def foo; yield([1, 2]); end
    foo { |a = 42, b = 42, *rest| p [a, b, rest] }
  }
  assert "[[1, 2], []]", %q{
    def foo; yield([1, 2]); end
    foo { |a = 42, *rest| p [a, rest] }
  }

  assert 'nil', 'p = proc { |x,| p x }; p.call'
  assert '42', 'p = proc { |x,| p x }; p.call(42)'
  assert '42', 'p = proc { |x,| p x }; p.call(42,1,2,3)'
  assert '42', 'p = proc { |x,| p x }; p.call([42])'
  assert '42', 'p = proc { |x,| p x }; p.call([42,1,2,3])'

  assert "true", "def foo; p block_given?; end; foo {}"
  assert "false", "def foo; p block_given?; end; foo"
  assert "false", "def foo; p block_given?; end; def bar; foo; end; bar {}"

  assert ':ok', "def foo; yield; end; begin; foo; rescue LocalJumpError; p :ok; end"

  assert ":ok", "def foo(&m); m.call; end; foo { p :ok }"
  assert ":ok", "def foo(&m); p :ok if m == nil; end; foo"

  assert "[[1, 0, 1, 0, 1], [0, 1, 1, 0, 0], [0, 0, 0, 1, 1], [0, 0, 0, 0, 0]]", %q{
    def trans(xs)
      (0..xs[0].size - 1).collect do |i|
        xs.collect{ |x| x[i] }
      end
    end
    p trans([1,2,3,4,5])
  }, :archs => ['i386']

  assert "[[1, 0, 1, 0, 1], [0, 1, 1, 0, 0], [0, 0, 0, 1, 1], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0]]", %q{
    def trans(xs)
      (0..xs[0].size - 1).collect do |i|
        xs.collect{ |x| x[i] }
      end
    end
    p trans([1,2,3,4,5])
  }, :archs => ['x86_64']

  assert '45', "p (5..10).inject {|sum, n| sum + n }"
  assert '151200', "p (5..10).inject(1) {|product, n| product * n }" 

  assert "42", "def foo(x); yield x; end; p = proc { |x| p x }; foo(42, &p)"
  assert "42", %q{
    class X
      def to_proc; proc { |x| p x }; end
    end
    def foo(x); yield x; end
    foo(42, &X.new)
  }
  assert "42", "def foo; yield; end; begin; foo(&Object.new); rescue TypeError; p 42; end"

  assert "42", "x = 0; proc { x = 42 }.call; p x"
  assert "42", "x = 0; p = proc { x += 40 }; x = 2; p.call; p x"

  assert "42", "n = 0; 100.times { |i| n += 1; break if n == 42 }; p n"
  assert "42", "n = 0; 100.times { |i| next if i % 2 == 0; n += 1; }; p n - 8"
  assert "42", "p 100.times { break 42 }"
  assert "42", "p proc { next 42 }.call"
  assert "42", "begin p proc { break 24 }.call rescue LocalJumpError; p 42 end"

  assert "42", "p [42].map { |x| x }.map { |y| y }[0]"

  assert '1302', %q{
    $count = 0
    def foo(v, x)
      x.times {
        x -= 1
        foo(v, x)
        $count += v
      }
    end
    foo(42, 5)
    p $count
  }

  assert '42', "x=42; 1.times { 1.times { 1.times { p x } } }"
  assert '42', "def f; 1.times { yield 42 }; end; f {|x| p x}"

  assert '42', "def foo; x = 42; proc { x }; end; p foo.call"
  assert '42', %q{
    def foo() x=1; [proc { x }, proc {|z| x = z}]; end
    a, b = foo
    b.call(42)
    p a.call
  }

  assert "2\n1", %{
    def f(x, y)
      1.times {
        f(2, false) if y
        p x
      }
    end
    f(1, true)
  }
end

test "exception" do

  assert ":ok", "begin; p :ok; rescue; end"
  assert ":ok", "begin; raise; p :nok; rescue; p :ok; end"
  assert ":ok", %q{
    def m; begin; raise; ensure; p :ok; end; end
    begin; m; rescue; end
  }

  assert "42", "x = 40; begin; x += 1; rescue; ensure; x += 1; end; p x"
  assert "42", "x = 40; begin; raise; x = nil; rescue; x += 1; ensure; x += 1; end; p x"

  assert "42", "x = begin; 42; rescue; nil; end; p x"
  assert "42", "x = begin; raise; nil; rescue; 42; end; p x"
  assert "42", "x = begin; 42; rescue; nil; ensure; nil; end; p x"
  assert "42", "x = begin; raise; nil; rescue; 42; ensure; nil; end; p x"

  assert "42", "x = 40; begin; x += 1; raise; rescue; retry if x < 42; end; p x"

  assert ":ok", %q{
    begin
      raise
    rescue => e
      p :ok if e.is_a?(RuntimeError)
    end
  }

  assert ":ok", %q{
    begin
      raise
    rescue => e
    end
    p :ok if e.is_a?(RuntimeError)
  }

  assert ":ok", %q{
    begin
      raise 'foo'
    rescue => e
      p :ok if e.is_a?(RuntimeError) and e.message == 'foo'
    end
  }

  assert ":ok", %q{
    class X < StandardError; end
    exc = X.new
    begin
      raise exc
    rescue => e
      p :ok if e == exc
    end
  }

  assert ":ok", %q{
    class X < StandardError; end
    class Y < X; end
    class Z < Y; end
    begin
      raise Y
    rescue Z
      p :nok
    rescue X
      p :ok
    end
  }

  assert ":ok", %q{
    begin
      begin
        raise LoadError
      rescue
        p :nok
      end
    rescue LoadError => e
      p :ok if e.is_a?(LoadError)
    end
  }, :known_bug => true

  assert ":ok", %q{
    begin
      self.foo
    rescue => e
      p :ok if e.is_a?(NoMethodError)
    end
  }

  assert ":ok", %q{
    begin
      foo
    rescue => e
      p :ok if e.is_a?(NameError)
    end
  }

  assert ":ok", %q{
    begin
      1.times { raise }
    rescue
      p :ok
    end
  }

  assert ":ok", %q{
    begin
      def foo; raise; end
      foo
    rescue
      p :ok
    end
  }

  assert ":ok", "1.times { x = foo rescue nil; }; p :ok"

end

test "ivar" do

  assert "nil", "p @foo"

  assert "42", %q{
    class Foo
      def initialize; @x = 42; end;
      def x; @x; end
    end
    p Foo.new.x
  }

  assert "42", %q{
    class Foo
      def initialize; @x, @y = 40, 2; end
    end
    class Foo
      def z; @z ||= @x + @y; end
    end
    p Foo.new.z
  }

  assert "42", %q{
    class Foo
      def initialize; @x = 42; end
    end
    o = Foo.new
    class Foo
      def y; @y ||= @x; end
    end
    p o.y
  }

  assert "42", %q{
    instance_variable_set(:@foo, 42)
    p instance_variable_get(:@foo)
  }

  assert "42", %q{
    class Foo
      def initialize; @x = 42; end
    end
    o = Foo.new
    p o.instance_variable_get(:@x)
  }

  assert "42\n123", %q{
    class Foo
       @foo = 42
       def self.foo; @foo; end
       def self.foo=(x); @foo=x; end
    end
    p Foo.foo
    Foo.foo=123
    p Foo.foo
  }

  assert '231', %q{
    class Foo
      def initialize
        @v1 = 1;   @v2 = 2;   @v3 = 3
        @v4 = 4;   @v5 = 5;   @v6 = 6
        @v7 = 7;   @v8 = 8;   @v9 = 9
        @v10 = 10; @v11 = 11; @v12 = 12
        @v13 = 13; @v14 = 14; @v15 = 15
        @v16 = 16; @v17 = 17; @v18 = 18
        @v19 = 19; @v20 = 20; @v21 = 21
      end
      def foo
        @v1 + @v2 + @v3 + @v4 + @v5 + @v6 +
        @v7 + @v8 + @v9 + @v10 + @v11 + @v12 +
        @v13 + @v14 + @v15 + @v16 + @v17 + @v18 +
        @v19 + @v20 + @v21
      end
    end
    p Foo.new.foo
  }

end

test "cvar" do

  assert ":ok", "begin; p @@foo; rescue NameError; p :ok; end"
  assert "42",  "@@foo = 42; p @@foo"

end

test "eval" do

  assert "42", "p eval('40 + 2')"
  assert "42", "def foo; 42; end; p eval('foo')"
  assert "42", "x = 40; p eval('x + 2')"
  assert "42", "x = 0; eval('x = 42'); p x"

  assert ":ok", "eval('x = 42'); begin; p x; rescue NameError; p :ok; end"

  assert "42", %q{
    def foo(b); x = 42; eval('x', b); end
    p foo(nil)
  }

  assert "42", %q{
    def foo(b); x = 43; eval('x', b); end
    x = 42
    p foo(binding)
  }

  assert "42", %q{
    def foo(b); x = 0; eval('x = 42', b); end
    x = 1
    foo(binding)
    p x
  }

  assert "42", %q{
    def foo; x = 123; bar {}; x; end
    def bar(&b); eval('x = 42', b.binding); end
    p foo
  }

  assert '42', %q{
    def foo; x = 42; proc {}; end
    p = foo; eval('p x', p.binding)
  }

  assert "42", %q{
    class Foo;
      def foo; 42; end;
      def bar(s, b); eval(s, b); end;
    end
    def foo; 123; end
    Foo.new.bar('p foo', nil)
  }

  assert "42", %q{
    class Foo;
      def foo; 43; end;
      def bar(s, b); eval(s, b); end;
    end
    def foo; 42; end
    Foo.new.bar('p foo', binding)
  }

  assert "42", "class A; def foo; @x; end; end; x = A.new; x.instance_eval { @x = 42 }; p x.foo"

  assert ":ok", "module M; module_eval 'p :ok'; end"
  assert ":ok", "module M; module_eval 'def self.foo; :ok; end'; end; p M.foo"

end

test "regexp" do

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

end

test "defined" do

  assert '"nil"',   "p defined? nil"
  assert '"self"',  "p defined? self"
  assert '"true"',  "p defined? true"
  assert '"false"', "p defined? false"

  assert '"expression"', "p defined? 123"
  assert '"expression"', "p defined? 'foo'"
  assert '"expression"', "p defined? [1,2,3]"
  assert '"expression"', "p defined? []"

  assert '"assignment"', "p defined? a=1"
  assert '"assignment"', "p defined? $a=1"
  assert '"assignment"', "p defined? @a=1"
  assert '"assignment"', "p defined? A=1"
  assert '"assignment"', "p defined? a||=1"
  assert '"assignment"', "p defined? a&&=1"
  assert '"assignment"', "1.times { |x| p defined? x=1 }"

  assert '"local-variable"', "a = 123; p defined? a"
  assert '"local-variable"', "1.times { |x| p defined? x }"

  assert 'nil', "p defined? @a"
  assert '"instance-variable"', "@a = 123; p defined? @a"

  assert 'nil', "p defined? $a"
  assert '"global-variable"', "$a = 123; p defined? $a"

  assert 'nil', "p defined? A"
  assert '"constant"', "A = 123; p defined? A"

end

test 'case' do

  assert 'nil', 'p case when false then 0 else end'
  assert 'nil', 'p case when true then else 1 end'
  assert '1', 'p case when false then 0 when true then 1 else 3 end'
  assert '1', 'p case when false then 0 when true then 1 end'
  assert '1', 'p case 1 when 1 then 1 else 2 end'
  assert '2', 'p case -1 when 1 then 1 else 2 end'
  assert ':fixnum', "p case 1 when Fixnum then :fixnum else :not_fixnum end"
  assert ':string', "p case '' when Fixnum then :fixnum when String then :string else :other end"
  assert ':fixnum_or_string', "p case '' when Fixnum, String then :fixnum_or_string else :other end"
  assert '1', 'p case 1 when 2, 1 then 1 else 2 end'
  assert '2', 'p case when false then 1 when nil, true then 2 else 3 end'
  assert "1\n2\n:foobar", %{
    def foo() p 1 end
    def bar() p 2 end
    p case 2
    when foo, bar
      :foobar
    end
  }
  assert "1\n:foobar", %{
    def foo() p 1 end
    def bar() p 2 end
    p case 1
    when foo, bar
      :foobar
    end
  }
  assert '1', 'p case 1 when *[2, 1] then 1 else 2 end'
  assert '1', 'a = [7, 8]; p case 1 when *a, *[4, 5], 1 then 1 else 2 end'

end

test "backquote" do

  assert '"foo\\n"', 'p `echo foo`'
  assert '"foo\\n"', 'def x; "foo"; end; p `echo #{x}`'

end

test "alias" do

  assert "42", "$foo = 42; alias $bar $foo; p $bar"
  assert "nil", "alias $bar $foo; p $bar"

  assert "42", "def foo; 42; end; alias :bar :foo; p bar"

end

test "require" do

  assert ":ok", %q{
    begin
      require 'doesnotexist'
    rescue LoadError
      p :ok
    end
  }

  assert ":ok", "$:.unshift('test_roxor_fixtures/lib'); require 'foo'"

  assert ":ok", "begin; require 'test_roxor_fixtures/lib/raise'; rescue NameError; p :ok; end"

end

test "method" do

  assert ":ok", %{
    def foo; :ok; end
    p method(:foo).call
  }

  assert "42", %{
    def foo(x); x; end
    p method(:foo).call(42)
  }

  assert ":ok", %{
    begin
      method(:does_not_exist)
    rescue NameError
      p :ok
    end
  }

  assert ":b\n:a", %{
    class A; def foo() :a end end
    class B < A; def foo() :b end end
    m = A.instance_method(:foo)
    b = B.new
    p b.foo, m.bind(b).call
  }

  assert '-5', "def f(a, b, d, g, c=1, e=2, f=3, *args); end; p method(:f).arity"
  assert '-5', "def f(a, b, d, g, c=1, e=2, f=3); end; p method(:f).arity"
  assert '-5', "def f(a, b, d, g, *args); end; p method(:f).arity"
  assert '4', "def f(a, b, d, g); end; p method(:f).arity"

end

test "io" do

  assert ":ok", "File.open('#{__FILE__}', 'r') { p :ok }"

  assert "true", "p(Dir['*.c'].length > 1)"
  assert "true", "p(Dir.glob('*.c').length > 1)"
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

end

test "encoding" do

  assert "US-ASCII", "File.open('Rakefile', 'r:US-ASCII') {|f| puts f.read.encoding.name }"

  assert ":ok", "puts ':ok'.encode('US-ASCII')"

end
