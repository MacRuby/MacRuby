# A regression performance suite for MacRuby (which doesn't really catch
# regressions yet).
#
# Run with:
#    $ ./miniruby -I./lib bench.rb
#
# The same script can also be executed using /usr/bin/ruby or ruby19 to 
# compare the implementations.

def fib(n)
  if n < 3
    1
  else
    fib(n-1) + fib(n-2)
  end
end

def tak(x, y, z)
  unless y < x
    z
  else
    tak(tak(x-1, y, z),
        tak(y-1, z, x),
        tak(z-1, x, y))
  end
end

def tarai(x, y, z)
  if x <= y
  then y
  else tarai(tarai(x-1, y, z),
             tarai(y-1, z, x),
             tarai(z-1, x, y))
  end
end

def ack(m, n)
  if m == 0 then
    n + 1
  elsif n == 0 then
    ack(m - 1, 1)
  else
    ack(m - 1, ack(m, n - 1))
  end
end

class Class1
  def method1; end
  def method2(x); x; end

  def yield1(n); i=0; while i<n; yield; i+=1; end; end
  def yield2; yield; end
end

class Class2 < Class1
  def method1; super; end
  def method2(x); super; end
end

def bench_ivar_read(n)
  @obj = 1
  i=0; while i<n; i+=@obj; end
end

def bench_ivar_write(n)
  @obj = 0
  i=0; while i<n; i+=@obj+1; end
end

ConstantOne = 1

require 'benchmark'

Benchmark.bm(30) do |bm|

  # Fixnum arithmetic.
  bm.report('10 fib(30)') do
    i=0; while i<10; fib(30); i+=1; end
  end
  bm.report('10 fib(35)') do
    i=0; while i<10; fib(35); i+=1; end
  end
  bm.report('tak') { tak(18,9,0) }
  bm.report('tarai') { tarai(12,6,0) }
  if RUBY_VERSION.to_f > 1.8
    # Ruby 1.8 is too weak for this benchmark.
    bm.report('ackermann') { ack(3,9) }
  end

  # Loops.
  bm.report('10000000 times loop') do
    3000000.times {}
  end 
  bm.report('30000000 times loop') do
    30000000.times {}
  end 
  bm.report('10000000 while loop') do
    i=0; while i<10000000; i+=1; end
  end 
  bm.report('60000000 while loop') do
    i=0; while i<60000000; i+=1; end
  end

  # Messages.
  bm.report('30000000 msg w/ 0 arg') do
    o = Class1.new
    i=0; while i<10000000; o.method1; o.method1; o.method1; i+=1; end 
  end
  bm.report('30000000 msg w/ 1 arg') do
    o = Class1.new
    i=0; while i<10000000; o.method2(i); o.method2(i); o.method2(i); i+=1; end 
  end
  bm.report('10000000 super w/ 0 arg') do
    o = Class2.new
    i=0; while i<10000000; o.method1; i+=1; end
  end
  bm.report('10000000 super w/ 1 arg') do
    o = Class2.new
    i=0; while i<10000000; o.method2(i); i+=1; end
  end
  bm.report('10000000 #send') do
    o = Class1.new
    i=0; while i<10000000; o.send(:method1); i+=1; end
  end

  # Instance variables.
  bm.report('10000000 ivar read') { bench_ivar_read(10000000) }
  bm.report('30000000 ivar read') { bench_ivar_read(30000000) }
  bm.report('10000000 ivar write') { bench_ivar_write(10000000) }
  bm.report('30000000 ivar write') { bench_ivar_write(30000000) }

  # Const lookup.
  bm.report('30000000 const') do
    i=0; while i<30000000; i+=ConstantOne; end
  end

  # Blocks
  bm.report('30000000 yield') do
    o = Class1.new
    o.yield1(30000000) {}
  end
  bm.report('30000000 msg w/ block+yield') do
    o = Class1.new
    i=0; while i<30000000; o.yield2 {}; i+=1; end
  end
  bm.report('30000000 Proc#call') do
    o = proc {}
    i=0; while i<30000000; o.call; i+=1; end
  end
  bm.report('30000000 dvar write') do
    i=0
    30000000.times { i=1 }
  end

  # Eval
  bm.report('1000 eval') do
    i=0
    s = "#{1+1}+#{20+20}"
    while i<1000
      eval(s)
      i+=1
    end
  end
  bm.report('30000000 binding-var write') do
    i=0
    eval('while i<30000000; i+=1; end')
  end

  # Break
  bm.report('30000000 while break') do
    i=0; while i<30000000; while true; i+=1; break; end; end
  end
  bm.report('10000000 block break') do
    i=0; while i<10000000; 1.times { i+=1; break }; end
  end

  # Next
  bm.report('30000000 while next') do
    i=0; while i<30000000; i+=1; next; exit; end
  end
  bm.report('10000000 block next') do
    i=0; while i<10000000; 1.times { i+=1; next; exit; }; end
  end

  # Exception handlers.
  bm.report('60000000 begin w/o exception') do
    i=0
    while i<60000000
      begin
        i+=1
      rescue
      end
    end
  end
  bm.report('60000000 ensure w/o exception') do
    i=0
    while i<60000000
      begin
        begin
        ensure
          i+=1
        end
      ensure
        i+=1
      end
    end
  end
  bm.report('50000 raise') do
    i=0
    while i<50000
      begin
        raise
      rescue
        i+=1
      end
    end
  end

  # Method
  bm.report('3000000 Method#call w/ 0 arg') do
    o = Class1.new
    m = o.method(:method1)
    i=0
    while i<3000000
      m.call
      i+=1
    end
  end
  bm.report('3000000 Method#call w/ 1 arg') do
    o = Class1.new
    m = o.method(:method2)
    i=0
    while i<3000000
      m.call(i)
      i+=1
    end
  end

end
