#!/usr/local/bin/macruby
#
# Iterate using different GCD techniques to illustrate relative performance/overhead
#
# Inspired by: https://developer.apple.com/mac/library/samplecode/Dispatch_Compared/index.html

require 'dispatch'
require 'benchmark'

$max_tasks = 256
$reps = 1024
$folds = 8
$results = nil#[]

$group = Dispatch::Group.new
$queue = Dispatch::Queue.new('org.macruby.gcd.serial')

class Benchmark
  def self.repeat(count, label="", &block)
    raise "count: #{count} < 1" if count < 1
    block.call
    t = measure {count.times &block} / count
    Tms.new(*t.to_a[1..-1], label)
  end
end

def work_function(i)
    x = 1.0+i*i
    $folds.times {|j| x = Math::tan(Math::PI/2 - Math::atan(Math::exp(2*Math::log(Math::sqrt(x))))) }
    $results[i] = x if not $results.nil?
end

def times(n)
  n.times {|i| work_function(i)}
end

def ptimes(n)
  n.p_times {|i| work_function(i)}
end

def apply(n)
  Dispatch::Queue.concurrent.apply(n) {|i| work_function(i)}
end

def concur(n)
  q = Dispatch::Queue.concurrent
  n.times do |i|
    q.async($group) {work_function(i)}
  end
  $group.wait
end

def serial(n)
  n.times {|i| $queue.async {work_function(i)}}
  $queue.sync { } 
end

def nqueue(n)
  n.times do |i|
    Dispatch::Queue.new("org.macruby.gcd.multi.#{i}").async($group) {work_function(i)}
  end
  $group.wait
end

def njobs(n)
  j = Dispatch::Job.new
  n.times {|i| j.add { work_function(i) }}
  j.join
end

def bench(method, count=1)
  proc = Proc.new { send(method.to_sym, count) }
  Benchmark.repeat($reps, "%6s" % method, &proc).real*1e6/count
end

METHODS = %w(times ptimes apply concur serial nqueue njobs)
TASKS = [t = 1]
TASKS << t *= 2 while t < $max_tasks

print "GCD BENCHMARKS\tMaxTask\t#{$max_tasks}\tFolds\t#{$folds}\tReps\t#{$reps}\n"
print "T µsec\t   #{TASKS.join("\t   ")}"

METHODS.each do |method|
  print "\n#{method}"
  TASKS.each do |n|
      print "\t%6.2f" % bench(method, n)
   end
end
puts
print "Results: #{$results.join("\t")}" if not $results.nil?
