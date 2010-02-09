# Calculate the value of an object in the background

module Dispatch
  # Subclass of Dispatch::Group used to implement lazy Futures
  # By returning a value and duck-typing Thread +join+ and +value+
   
  class Future < Dispatch::Group
    # Create a future that asynchronously dispatches the block 
    # to a concurrent queue of the specified (optional) +priority+
    def initialize(priority=nil, &block)
      super
      @value = nil
      Dispatch.group(self, priority) { @value = block.call }
    end

    # Waits for the computation to finish
    alias_method :join, :wait

    # Joins, then returns the value
    # If a block is passed, invoke that asynchronously with the final value
    # on the specified +queue+ (or else the default queue).
    def value(queue = nil, &callback)
      if not block_given?
        wait
        return @value
      else
        queue ||= Dispatch::Queue.concurrent
        notify(queue) { callback.call(@value) }
      end
    end   
  end

  # Run the +&block+ asynchronously on a concurrent queue of the given
  # (optional) +priority+ as part of a Future, which is returned for use with
  # +join+ or +value+ -- or as a Group, of which it is a subclass
  
  def fork(priority=nil, &block)
    Dispatch::Future.new(priority) &block
  end

  module_function :fork

end
