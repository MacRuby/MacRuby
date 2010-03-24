#!/usr/local/bin/macruby

require 'dispatch'
require 'benchmark'

class Benchmark
  def self.repeat(count, label="", &block)
    raise "count: #{count} < 1" if count < 1
    block.call
    t = measure {count.times &block} / count
    Tms.new(*t.to_a[1..-1], label).inspect
  end
end

$max_tasks = 256
$reps = 1024
$folds = 32
$results = nil#[]

puts "GCD Benchmarks"
puts "Tasks: #{$max_tasks}\tFolded: #{$folds}\tReps:\t#{$reps}"

def work_function(i)
    x = 1.0+i*i
    $folds.times {|j| x = Math::tan(Math::PI/2 - Math::atan(Math::exp(2*Math::log(Math::sqrt(x))))) }
    $results[i] = x if not $results.nil?
end

def iter(n)
  n.times {|i| work_function(i)}
end

def p_iter(n)
  n.p_times {|i| work_function(i)}
end

def apply(n)
  Dispatch::Queue.concurrent.apply(n) {|i| work_function(i)}
end

def concur(n)
  j = Dispatch::Job.new
  n.times {|i| j.add { work_function(i) }}
  j.join
end

def serial(n)
  q = Dispatch::Queue.new('org.macruby.gcd.serial')
  n.times {|i| q.async {work_function(i)}}
  q.sync { } 
end

def multiq(n)
  g = Dispatch::Group.new
  n.times do |i|
    Dispatch::Queue.new("org.macruby.gcd.multi.#{i}").async(g) {work_function(i)}
  end
  g.wait
end

def bench(method, count=1)
  proc = Proc.new { send(method.to_sym, count) }
  t = Benchmark.measure("%6s" % method, &proc)
  t_msec = t.real*1000
  puts "#{t.label}: %5.2f millisec (avg: %5.2f microsec)" % [t_msec, t_msec*1000/count]
end

n = 1
while n <= $max_tasks do
  puts "\n#{n} tasks"
  %w(iter p_iter apply concur serial multiq).each do |s|
    bench(s, n)
  end
  n *= 2
end

puts "Results: #{$results}" if not $results.nil?
