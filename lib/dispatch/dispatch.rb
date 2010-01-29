# Convenience methods for calling the concurrent queues 
# directly from the top-level Dispatch module

module Dispatch
  # Returns a new serial queue with a unique label based on
  # the ancestor chain and ID of +obj+
  def queue_for(obj)
    label = obj.class.ancestors.reverse.join(".").downcase
    Dispatch::Queue.new("#{label}.%x" % obj.object_id)
  end

  # Run the +&block+ asynchronously on a concurrent queue
  # of the given (optional) +priority+ 
  def async(priority=nil, &block)
    Dispatch::Queue.concurrent(priority).async &block
  end
  
  # Run the +&block+ synchronously on a concurrent queue
  # of the given (optional) +priority+ 
  def sync(priority=nil, &block)
    Dispatch::Queue.concurrent(priority).sync &block
  end

  # Run the +&block+ asynchronously on a concurrent queue
  # of the given (optional) +priority+ as part of the specified +grp+
  def group(grp, priority=nil, &block)
    Dispatch::Queue.concurrent(priority).async(grp) &block
  end

  # Wrap the passed +obj+ (or its instance) inside an Actor to serialize access
  # and allow asynchronous invocation plus a callback
  def wrap(obj)
    Dispatch::Actor.new( (obj.is_a? Class) ? obj.new : obj)
  end

  # Run the +&block+ asynchronously on a concurrent queue
  # of the given (optional) +priority+ 
  # as part of a newly-created group, which is returned for use with
  # +Dispatch.group+ or +wait+ / +notify+
  
  def fork(priority=nil, &block)
    grp = Group.new
    Dispatch.group(grp) &block
    return grp
  end

  class Group
    # Companion to +Dispatch.fork+, allowing you to +wait+ until +grp+ completes
    # providing an API similar to that used by +Threads+
    # if a block is given, instead uses +notify+ to call it asynchronously
    def join(&block)
      block_given? ? notify &block : wait
    end
  end

end