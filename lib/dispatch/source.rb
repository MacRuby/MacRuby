# Adds convenience methods to Queues for creating Sources

module Dispatch
  class Source
    
    @@events = {
      exit:PROC_EXIT,
      fork:PROC_FORK,
      exec:PROC_EXEC,
      signal:PROC_SIGNAL, 
      
      delete:VNODE_DELETE,
      write:VNODE_WRITE, 
      extend:VNODE_EXTEND, 
      attrib:VNODE_ATTRIB, 
      link:VNODE_LINK, 
      rename:VNODE_RENAME, 
      revoke:VNODE_REVOKE
    }
      
    class << self
          
      def event2num(e)
        return 0 if e.nil?
        value = e.to_int rescue @@events[e.to_sym]
        raise ArgumentError, "No event type #{e.inspect}" if value.nil?
        value
      end
    
      def events2mask(events)
        mask = events.collect { |e| event2num(e) }.reduce(:|)
      end

      def data2events(bitmask)
        @@events.collect { |k,v| k if (v & bitmask) > 0 }.compact
      end

      # Returns Dispatch::Source of type DATA_ADD
      def add(queue = Dispatch::Queue.concurrent, &block)
        Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0, 0, queue, &block)
      end

      # Returns Dispatch::Source of type DATA_OR
      def or(queue = Dispatch::Queue.concurrent, &block)
        Dispatch::Source.new(Dispatch::Source::DATA_OR, 0, 0, queue, &block)
      end

      # Takes events: :delete, :write, :extend, :attrib, :link, :rename, :revoke
      # Returns Dispatch::Source of type PROC
      def process(pid, events, queue = Dispatch::Queue.concurrent, &block)
        events = events2mask(events) if not events.respond_to? :to_int
        Dispatch::Source.new(Dispatch::Source::PROC, pid, events, queue, &block)
      end

      # Returns Dispatch::Source of type SIGNAL
      def signal(signal, queue = Dispatch::Queue.concurrent, &block)
        signal = Signal.list[signal.to_s] if signal.to_i == 0
        Dispatch::Source.new(Dispatch::Source::SIGNAL, signal, 0, queue, &block)
      end

      # Returns Dispatch::Source of type READ
      def read(file, queue = Dispatch::Queue.concurrent, &block)
        Dispatch::Source.new(Dispatch::Source::READ, file, 0, queue, &block)
      end

      # Returns Dispatch::Source of type WRITE
      def write(file, queue = Dispatch::Queue.concurrent, &block)
        Dispatch::Source.new(Dispatch::Source::WRITE, file, 0, queue, &block)
      end

      # Takes events: :exit, :fork, :exec, :signal
      # Returns Dispatch::Source of type VNODE
      def file(file, events, queue = Dispatch::Queue.concurrent, &block)
        events = events2mask(events) if not events.respond_to? :to_int
        Dispatch::Source.new(Dispatch::Source::VNODE, file, events, queue, &block)
      end

      def periodic(seconds, queue = Dispatch::Queue.concurrent, &block)
        Dispatch::Source.timer(0, seconds, 0, queue, &block)
      end
    end
  end
end
