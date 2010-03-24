#!/usr/local/bin/macruby

require 'dispatch'
require 'benchmark'

class Benchmark
  def self.repeat(count, label="", &block)
    raise "count: #{count} < 1" if count < 1
    block.call
    t = measure {count.times &block} / count
    Tms.new(*t.to_a[1..-1], label)
  end
end

$max_tasks = 256
$reps = 1024
$folds = 32
$results = nil#[]

METHODS = %w(iter p_iter apply concur serial nqueue njobs)

puts "GCD BENCHMARKS"
puts "Folds,\t#{$folds},\tMaxTasks,\t#{$max_tasks},\tReps,\t#{$reps}"
puts "TYPE,\tTASKS,\t'TIME µsec'"

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
  g = Dispatch::Group.new
  q = Dispatch::Queue.concurrent
  n.times do |i|
    q.async(g) {work_function(i)}
  end
  g.wait
end

def serial(n)
  q = Dispatch::Queue.new('org.macruby.gcd.serial')
  n.times {|i| q.async {work_function(i)}}
  q.sync { } 
end

def nqueue(n)
  g = Dispatch::Group.new
  n.times do |i|
    Dispatch::Queue.new("org.macruby.gcd.multi.#{i}").async(g) {work_function(i)}
  end
  g.wait
end

def njobs(n)
  j = Dispatch::Job.new
  n.times {|i| j.add { work_function(i) }}
  j.join
end

def bench(method, count=1)
  proc = Proc.new { send(method.to_sym, count) }
  t = Benchmark.repeat($reps, "%6s" % method, &proc)
  puts "#{method},\t#{count},\t%6.2f" % (t.real*1e6/count)
end

n = 1
while n <= $max_tasks do
  METHODS.each { |method| bench(method, n) }
  n *= 2
end

puts "Results: #{$results}" if not $results.nil?
