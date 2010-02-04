# Calculate the value of an object in the background

module Dispatch
  # Wrapper around Dispatch::Group used to implement lazy Futures 
  class Future
    # Create a future that asynchronously dispatches the block 
    # to a concurrent queue of the specified (optional) +priority+
    def initialize(priority=nil, &block)
      @value = nil
      @group = Dispatch.fork(priority) { @value = block.call }
    end

    # Waits for the computation to finish, then returns the value
    # Duck-typed to lambda.call(void)
    # If a block is passed, invoke that asynchronously with the final value
    def call(q = nil, &callback)
      if not block_given?
        @group.wait
        return @value
      else
        q ||= Dispatch::Queue.concurrent
        @group.notify(q) { callback.call(@value) }
      end
    end
        
  end
end
