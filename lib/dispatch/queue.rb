# Adds convenience methods to Queues

module Dispatch
  class Queue

    # Returns a mostly unique reverse-DNS-style label based on
    # the ancestor chain and ID of +obj+ plus the current time
    # 
    #   Dispatch::Queue.labelize(Array.new)
    #   => enumerable.array.0x2000cc2c0.1265915278.97557
    #
    def self.labelize(obj)
      names = obj.class.ancestors[0...-2].map {|a| a.to_s.downcase}
      label = names.uniq.reverse.join(".")
      "#{label}.0x%x.#{Time.now.to_f}" % obj.object_id
    end

    # Returns a new serial queue with a unique label based on +obj+
    # Typically used to serialize access to that object
    #
    #   a = Array.new
    #   q = Dispatch::Queue.for(a)
    #   q.async { a << 2 }
    #
    def self.for(obj)
      new(labelize(obj))
    end
    
    def join
      sync {}
    end
  end
end
