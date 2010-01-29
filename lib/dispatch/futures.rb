module Dispatch
  # Wrapper around Dispatch::Group used to implement lazy Futures 
  class Future
    # Create a future that asynchronously dispatches the block 
    # to a concurrent queue of the specified (optional) +priority+
    def initialize(priority=nil, &block)
      @group = Group.new
      @value = nil
      Dispatch.group(@group, priority) { @value = block.call }
    end

    # Waits for the computation to finish, then returns the value
    # Duck-typed to lambda.call(void)
    def call()
      @group.wait
      @value
    end

    # Passes the value to the +callback+ block when it is available
    # Duck-typed to group.notify(&block)
    def notify(&callback)
      @group.notify { callback.call(@value) }
    end
  end
end
