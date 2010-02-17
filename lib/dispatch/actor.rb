require 'delegate'

module Dispatch
  # Serialize or asynchronize access to a delegate object.
  # Forwards method invocations to the passed object via a private serial queue, 
  # and optionally calls back asynchronously (if given a block or group).
  #
  # Note that this will NOT work for methods that themselves expect a block.
  # For those, use e.g., the Enumerable p_* methods insteads. 
  class Actor < SimpleDelegator
    
    # Create an Actor to wrap the given +delegate+,
    # optionally specifying the default +callback+ queue
    def initialize(delegate, callback=nil)
      super(delegate)
      @callback = callback || Dispatch::Queue.concurrent
      @q = Dispatch::Queue.new("dispatch.actor.#{delegate}.#{object_id}")
    end
        
    # Specify the +callback+ queue for async requests
    # => self to allow chaining
    def _on_(callback)
      @callback = callback
      self
    end

    # Specify or return a +group+ for private async requests
    def __group__(group=nil)
      @group = group  || Group.new
    end

    # Wait until the internal private queue has completed pending executions
    # then return the +delegate+ object
    def _done_
      @q.sync { }
      __getobj__
    end

    # Calls the +delegate+ object asychronously if there is a block or group,
    # else synchronously
    def method_missing(symbol, *args, &block)
      if block_given? or not @group.nil?
        callback = @callback
        @q.async(@group) do
          retval = __getobj__.__send__(symbol, *args)
          callback.async { block.call(retval) }  if not block.nil?
        end
        return nil
      else
        @retval = nil
        @q.sync { @retval = __getobj__.__send__(symbol, *args) }
        return @retval
      end
    end

  end
end
