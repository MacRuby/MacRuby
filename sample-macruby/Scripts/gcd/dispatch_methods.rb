#!/usr/local/bin/macruby
require 'dispatch'

puts "\n Use Dispatch.async to do stuff in the background"
Dispatch.async { p "Did this later" }
sleep 0.1

puts "\n Use Dispatch.group to track when stuff completes"
g = Dispatch.group { p "Do this" }
Dispatch.group(g) { p "and that" }
g.wait
p "Done"

puts "\n Use Dispatch.fork to capture return values in a Future"
f = Dispatch.fork {  2+2  }
p f.value
puts "  - pass a block to return the value asynchronously"
f.value { |v| p "Returns #{v}" }
sleep 0.1

puts "\n Use Dispatch.queue_for to create a private serial queue"
puts "  - synchronizes access to shared data structures"
a = Array.new
q = Dispatch.queue_for(a)
puts "  - has a (mostly) unique name:"
p q
q.async { a << "change me"  }
puts "  - uses sync to block and flush queue"
q.sync { p a }

puts "\n Use with a group for more complex dependencies, "
q.async(g) { a << "more change"  }
Dispatch.group(g) do 
  tmp = "complex calculation"
  q.async(g) { a << tmp }
end
puts "  - uses notify to execute block when done"
g.notify(q) { p a }
q.sync {}

puts "\n Use Dispatch.wrap to serialize object using an Actor"
b = Dispatch.wrap(Array)
b << "safely change me"
p b.size # => 1 (synchronous return)
b.size {|n| p "Size=#{n}"} # => "Size=1" (asynchronous return)
