#!/usr/local/bin/macruby
require 'dispatch'

puts "\n Use Dispatch.async to do stuff on another thread or core) "
Dispatch.async { p "Did this elsewhere" }
sleep 0.1 # => "Did this elsewhere"

puts "\n Use Dispatch.group to track when async stuff completes"
g = Dispatch.group { p "Do this" }
Dispatch.group(g) { p "and that" }
g.wait # => "Do this" "and that"
p "Done"

puts "\n Use Dispatch.fork to capture return values of async stuff"
f = Dispatch.fork {  2+2  }
p f.value # => 4
puts "  - pass a block to return the value asynchronously"
f.value { |v| p "Returns #{v}" }
sleep 0.1  # => "Returns 4"

puts "\n Use Dispatch.queue to create a private serial queue"
puts "  - synchronizes access to shared data structures"
a = Array.new
q = Dispatch.queue(a)
puts "  - has a (mostly) unique name:"
p q # => Dispatch.enumerable.array.0x2000a6920.1266002369.9854
q.async { a << "change me"  }
puts "  - uses sync to block and flush queue"
q.sync { p a } # => ["change me"]

puts "\n Use with a group for more complex dependencies, "
q.async(g) { a << "more change" }
Dispatch.group(g) do 
  tmp = "complex calculation"
  q.async(g) { a << tmp }
end
puts "  - uses notify to execute block when done"
g.notify(q) { p a }
q.sync {} # => ["change me", "more change", "complex calculation"]

puts "\n Use Dispatch.wrap to serialize object using an Actor"
b = Dispatch.wrap(Array)
b << "safely change me"
p b.size # => 1 (synchronous return)
b.size {|n| p "Size=#{n}"}
sleep 0.1  # => "Size=1" (asynchronous return)
