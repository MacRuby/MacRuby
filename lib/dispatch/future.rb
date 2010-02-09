# Calculate the value of an object in the background

module Dispatch
  # Wrap Dispatch::Group to implement lazy Futures
  # By duck-typing Thread +join+ and +value+
   
  class Future
    # Create a future that asynchronously dispatches the block 
    # to the default queue
    attr_accessor :group
    
    def initialize(priority = nil, &block)
      @value = nil
      @group = Group.new
      Dispatch.group(@group, priority) { @value = block.call }
    end

    # Waits for the computation to finish
    def join
      group.wait
    end

    # Joins, then returns the value
    # If a block is passed, invoke that asynchronously with the final value
    # on the specified +queue+ (or else the default queue).
    def value(queue = nil, &callback)
      if not block_given?
        group.wait
        return @value
      else
        queue ||= Dispatch::Queue.concurrent
        group.notify(queue) { callback.call(@value) }
      end
    end   
  end

end
