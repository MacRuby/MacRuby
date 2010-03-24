#!/usr/local/bin/macruby

require 'benchmark'
require 'dispatch'

$max_tasks = 256
$benched = $reps = 100
$folds = 32
$results = nil#[]

puts "GCD Benchmarks"
puts "Tasks: #{$max_tasks}\tFolded: #{$folds}\tReps:\t#{$reps}\n"

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

def job(n)
  j = Dispatch::Job.new
  n.times {|i| j.add { work_function(i) }}
  j.join
end

def queue(n)
  q = Dispatch::Queue.new('org.macruby.gcd.serial')
  n.times {|i| q.async {work_function(i)}}
  q.sync { } 
end

def multi(n)
  g = Dispatch::Group.new
  n.times do |i|
    Dispatch::Queue.new("org.macruby.gcd.multi.#{i}").async(g) {work_function(i)}
  end
  g.wait
end


def run(x, label, &block)
  (x.nil?) ? yield : x.report(label) {$reps.times &block}
end

def run_all(x, n)
    puts "#{n} tasks" if not x.nil?
    run(x,"looped") { iter(n) }
    run(x,"p_loop") { p_iter(n) }
    run(x," apply") { apply(n) }
    run(x,"concur") { job(n) }
    run(x,"serial") { queue(n) }
    run(x,"multiq") { multi(n) }
    puts
end

n = 1
width = 6
while n <= $max_tasks do
  run_all(nil, n)
  Benchmark.bm(width) { |x| run_all(x, n) }
  n *= 2
end

puts "Results: #{$results}" if not $results.nil?
