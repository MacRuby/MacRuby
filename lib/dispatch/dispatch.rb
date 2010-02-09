# Convenience methods for calling the concurrent queues 
# directly from the top-level Dispatch module

module Dispatch
  # Returns a unique label based on the ancestor chain and ID of +obj+
  # plus the current time
  def label_for(obj)
    ancestors = obj.class.ancestors.map {|a| a.to_s}
    label = ancestors.uniq.reverse.join("_").downcase
    now = Time.now.to_f.to_s.gsub(".","_")
    "#{label}__%x__%s" % [obj.object_id, now]
  end

  # Returns a new serial queue with a unique label based on +obj+
  def queue_for(obj)
    Dispatch::Queue.new Dispatch.label_for(obj)
  end

  # Run the +&block+ synchronously on a concurrent queue
  # of the given (optional) +priority+ 
  def sync(priority=nil, &block)
    Dispatch::Queue.concurrent(priority).sync &block
  end

  # Run the +&block+ asynchronously on a concurrent queue
  # of the given (optional) +priority+ 
  def async(priority=nil, &block)
    Dispatch::Queue.concurrent(priority).async &block
  end
  
  # Run the +&block+ asynchronously on a concurrent queue
  # of the given (optional) +priority+ as part of the specified +grp+
  def group(grp, priority=nil, &block)
    Dispatch::Queue.concurrent(priority).async(grp) { block.call }
    # Can't pass block directly for some reason
  end

  # Wrap the passed +obj+ (or its instance) inside an Actor to serialize access
  # and allow asynchronous invocation plus a callback
  def wrap(obj)
    Dispatch::Actor.new( (obj.is_a? Class) ? obj.new : obj)
  end

  # Run the +&block+ asynchronously on a concurrent queue of the given
  # (optional) +priority+ as part of a Future, which is returned for use with
  # +join+ or +value+ -- or as a Group, of which it is a subclass
  
  def fork(priority=nil, &block)
    Dispatch::Future.new(priority) &block
  end
  
  module_function :label_for, :queue_for, :async, :sync, :group, :wrap, :fork

end
