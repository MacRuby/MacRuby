#!/usr/local/bin/macruby

# An implementation of futures (delayed computations) on top of GCD.
# Original implementation written by Patrick Thomson.
# Improvements made by Ben Stiglitz.

include Dispatch

class Future
  def initialize(&block)
    # Each thread gets its own FIFO queue upon which we will dispatch
    # the delayed computation passed in the &block variable.
    Thread.current[:futures] ||= Queue.new("org.macruby.futures-#{Thread.current.object_id}")
    # Groups are just simple layers on top of semaphores.
    @group = Group.new
    # Asynchronously dispatch the future to the thread-local queue.
    Thread.current[:futures].async(@group) { @value = block[] }
  end
  
  def value
    # Wait fo the computation to finish. If it has already finished, then
    # just return the value in question.
    @group.wait
    @value
  end
end


f = Future.new do
  sleep 2.5
  'some value'
end
 
p f.value