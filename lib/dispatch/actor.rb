require 'delegate'

module Dispatch
  # Create an Actor that serializes or asynchronizes access to a delegate.
  # Forwards method invocations to the passed object via a private serial queue, 
  # and optionally calls back asynchronously (if given a block or group).
  #
  # Note that this will NOT work for methods that themselves expect a block.
  # For those, use e.g., the Enumerable p_* methods insteads. 
  #
  class Actor < SimpleDelegator
    
    # Create an Actor to wrap the given +delegate+,
    # optionally specifying the default +callback+ queue
    def initialize(delegate, callback=nil)
      super(delegate)
      @callback = callback || Dispatch::Queue.concurrent
      @q = Dispatch::Queue.new("dispatch.actor.#{delegate}.#{object_id}")
    end
        
    # Specify the +callback+ queue for async requests
    def _on_(callback)
      @callback = callback
      self
    end

    # Specify the +group+ for async requests
    def _with_(group)
      @group = group
      self
    end

    # Wait until the internal private queue has completed execution
    # then returns the +delegate+ object
    def _done_
      @q.sync { }
      __getobj__
    end
    
    def method_missing(symbol, *args, &block)
      if block_given? or not @group.nil?
        #puts "\nAsync #{symbol.inspect}"
        callback = @callback
        @q.async(@group) do
          retval = __getobj__.__send__(symbol, *args)
          callback.async { block.call(retval) } if not callback.nil?
        end
        return nil
      else
        #puts "\nSync #{symbol.inspect}" if symbol != :__
        @retval = nil
        @q.sync { @retval = __getobj__.__send__(symbol, *args) }
        return @retval
      end
    end
    
  end
end
