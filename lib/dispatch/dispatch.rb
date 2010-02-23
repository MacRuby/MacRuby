# Convenience methods for invoking GCD 
# directly from the top-level Dispatch module

module Dispatch

  # Asynchronously run the +&block+ on a concurrent queue
  # as part a +job+ -- which is returned for use with +join+ or +value+ 
  def fork(job=nil, &block)
    Dispatch::Queue.concurrent.fork(job) &block
  end

  # Returns a mostly unique reverse-DNS-style label based on
  # the ancestor chain and ID of +obj+ plus the current time
  # 
  #   Dispatch.labelize(Array.new)
  #   => Dispatch.enumerable.array.0x2000cc2c0.1265915278.97557
  #
  def labelize(obj)
    names = obj.class.ancestors[0...-2].map {|a| a.to_s.downcase}
    label = names.uniq.reverse.join(".")
    "#{self}.#{label}.%p.#{Time.now.to_f}" % obj.object_id
  end

  # Returns a new serial queue with a unique label based on +obj+
  # (or self, if no object is specified)
  # used to serialize access to objects called from multiple threads
  #
  #   a = Array.new
  #   q = Dispatch.queue(a)
  #   q.async {a << 2 }
  #
  def queue(obj=self, &block)
    q = Dispatch::Queue.new(Dispatch.labelize(obj))
    q.async { block.call } if not block.nil?
    q
  end

  # Wrap the passed +obj+ (or its instance, if a Class) inside an Actor
  # to serialize access and allow asynchronous returns
  #
  #   a = Dispatch.wrap(Array)
  #   a << Time.now # automatically serialized
  #   a.size # => 1 (synchronous return)
  #   a.size {|n| p "Size=#{n}"} # => "Size=1" (asynchronous return)
  #
  def wrap(obj)
    Dispatch::Actor.new( (obj.is_a? Class) ? obj.new : obj)
  end

  module_function :fork, :labelize, :queue, :wrap

end
