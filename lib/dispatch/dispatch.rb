# Convenience methods for invoking GCD 
# directly from the top-level Dispatch module

module Dispatch
  # Asynchronously run the +&block+  
  # on a concurrent queue of the given (optional) +priority+
  #
  #   Dispatch.async {p "Do this later"}
  # 
  def async(priority=nil, &block)
    Dispatch::Queue.concurrent(priority).async &block
  end

  # Asynchronously run the +&block+ inside a Future
  # -- which is returned for use with +join+ or +value+ --
  # on a concurrent queue of the given (optional) +priority+
  #
  #   f = Dispatch.fork { 2+2 }
  #   f.value # => 4
  # 
  def fork(priority=nil, &block)
    Dispatch::Future.new(priority) { block.call }
  end

  # Asynchronously run the +&block+ inside a Group
  # -- which is created if not specified, and
  # returned for use with +wait+ or +notify+ --
  # on a concurrent queue of the given (optional) +priority+
  #
  #   g = Dispatch.group {p "Did this"}
  #   g.wait # => "Did this"
  # 
  def group(grp=nil, priority=nil, &block)
    grp ||= Dispatch::Group.new
    Dispatch::Queue.concurrent(priority).async(grp) { block.call }
    grp
  end

  # Returns a mostly unique reverse-DNS-style label based on
  # the ancestor chain and ID of +obj+ plus the current time
  # 
  #   Dispatch.label_for(Array.new)
  #   => Dispatch.enumerable.array.0x2000cc2c0.1265915278.97557
  #
  def label_for(obj)
    names = obj.class.ancestors[0...-2].map {|a| a.to_s.downcase}
    label = names.uniq.reverse.join(".")
    "#{self}.#{label}.%p.#{Time.now.to_f}" % obj.object_id
  end

  # Returns a new serial queue with a unique label based on +obj+
  # used to serialize access to objects called from multiple threads
  #
  #   a = Array.new
  #   q = Dispatch.queue_for(a)
  #   q.async {a << Time.now }
  #
  def queue_for(obj)
    Dispatch::Queue.new Dispatch.label_for(obj)
  end

  # Wrap the passed +obj+ (or its instance, if a Class) inside an Actor
  # to serialize access and allow asynchronous returns
  #
  #   a = Dispatch.wrap(Array)
  #   a << Time.now # automatically serialized
  #   a.size # => 1 (synchronously)
  #   a.size {|n| p "Size=#{n}"} # => "Size=1" (asynchronously)
  #
  def wrap(obj)
    Dispatch::Actor.new( (obj.is_a? Class) ? obj.new : obj)
  end

  module_function :async, :fork, :group, :label_for, :queue_for, :wrap

end
