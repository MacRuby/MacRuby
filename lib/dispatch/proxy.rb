require 'delegate'

module Dispatch
  # Serialize or asynchronize access to a delegate object.
  # Forwards method invocations to the passed object via a private serial queue, 
  # and can call back asynchronously if given a block
  #
  class Proxy < SimpleDelegator
    
    attr_accessor :__group__, :__queue__, :__sync__
    
    # Create Proxy to wrap the given +delegate+,
    # optionally specify +group+ and +queue+ for asynchronous callbacks
    def initialize(delegate, group=Group.new, queue=Dispatch::Queue.concurrent)
      super(delegate)
      @__serial__ = Dispatch::Queue.for(self)
      @__group__ = group
      @__queue__ = queue
      @__retval__ = nil
    end

    # Call methods on the +delegate+ object via a private serial queue
    # Returns asychronously if given a block; else synchronously
    #
    def method_missing(symbol, *args, &block)
      if block.nil? then
        @__serial__.sync { @__retval__ = __getobj__.__send__(symbol,*args) }
        return @__retval__
      end
      queue = @__queue__ # copy in case it changes while in flight
      @__serial__.async(@__group__) do
        retval = __getobj__.__send__(symbol, *args)
        queue.async(@__group__) { block.call(retval) }
      end
    end

    # Wait until the internal private queue has completed pending executions
    def __wait__
      @__serial__.sync { }
    end
    
    # Return the +delegate+ object after waiting
    def __value__
      __wait__
      __getobj__
    end
    
  end
end
