# Provide an easy, uniform wrapper around +wait+ and +notify+

module Dispatch
  # Extend Dispatch::Group 
   
  # Implements "join" in Group for compatibility with Future
  # TODO: make Future a subclass of Group
  #       Currently not possible: https://www.macruby.org/trac/ticket/613
  class Group
    # Waits for the group to complete
    # If a block is specified, call on the specified queue or priority, if any
    def join(queue = Dispatch::Queue.concurrent, &block)
      return wait if block.nil?
      queue = Dispatch::Queue.concurrent(queue) if not queue.is_a? Dispatch::Queue #i.e., a priority
      notify(queue) { block.call }
    end
  end
  
end
