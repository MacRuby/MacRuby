module Dispatch
  
  # Track completion and return values of asynchronous requests
  # Duck-type +join+ and +value+ from +Thread+
  class Job  
    # Create a Job that asynchronously dispatches the block 
    attr_accessor :group
    
    def initialize(queue = Dispatch::Queue.concurrent, &block)
      @value = nil
      @group = Group.new
      queue.async(@group) { @value = block.call }
    end

    # Waits for the computation to finish, or calls block (if present) when done
    def join(&block)
      group.join(&block)
    end

    # Joins, then returns the value
    # If a block is passed, invoke that asynchronously with the final value
    # on the specified +queue+ (or else the default queue).
    def value(queue = Dispatch::Queue.concurrent, &callback)
      return group.notify(queue) { callback.call(@value) } if not callback.nil?
      group.wait
      return @value
    end
  end

end
