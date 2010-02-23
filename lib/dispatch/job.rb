module Dispatch
  
  # Track completion and return values of asynchronous requests
  # Duck-type +join+ and +value+ from +Thread+
  class Job  
    # Create a Job that asynchronously dispatches the block 
    attr_reader :group, :results
    
    def initialize(queue = Dispatch::Queue.concurrent, &block)
      @queue = queue
      @group = Group.new
      @results = synchronize([])
      add(&block) if not block.nil?
    end
    
    def synchronize(obj)
      Dispatch::Proxy.new(obj, @group)
    end
    
    # Submit block as part of the same dispatch group
    def add(&block)
      @queue.async(@group) { @results << block.call }      
    end
  
    # Wait until execution has completed.
    # If a +block+ is passed, invoke that asynchronously
    # on the specified +queue+ (or else the default queue).
    def join(queue = Dispatch::Queue.concurrent, &block)
      return group.wait if block.nil?
      group.notify(queue) { block.call } 
    end
  
    # Wait then return the next value; note: only ordered if a serial queue
    # If a +block+ is passed, invoke that asynchronously with the value
    # on the specified +queue+ (or else the default queue).
    def value(queue = Dispatch::Queue.concurrent, &block)
      return group.notify(queue) { block.call(result) } if not block.nil?
      group.wait
      return result
    end

    alias_method :sync, :synchronize

    private
    
    # Remove and return the first value    
    def result
       @results.shift
    end
    
  end
end
