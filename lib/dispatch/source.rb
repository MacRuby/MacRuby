# Adds convenience methods to Queues for creating Sources

module Dispatch
  class Source
    
    @@proc_events = {
       exit:PROC_EXIT,
       fork:PROC_FORK,
       exec:PROC_EXEC,
       signal:PROC_SIGNAL
       }

    @@vnode_events = {
      delete:VNODE_DELETE,
      write:VNODE_WRITE, 
      extend:VNODE_EXTEND, 
      attrib:VNODE_ATTRIB, 
      link:VNODE_LINK, 
      rename:VNODE_RENAME, 
      revoke:VNODE_REVOKE
      }
      
    class << self
      
      def proc_event(e)
        convert_event(e, @@proc_events)
      end

      def vnode_event(e)
        convert_event(e, @@vnode_events)
      end
    
      def events2mask(events, hash)
        mask = events.collect { |e| convert_event(e, hash) }.reduce(:|)
      end
      
      def convert_event(e, hash)
        return e.to_i if e.is_a? Numeric
        value = hash[e.to_sym]
        raise ArgumentError, "No event type #{e.inspect}" if value.nil?
        value
      end
    

      # Returns a Dispatch::Source::DATA_ADD
      def on_add(queue = Dispatch::Queue.concurrent, &block)
        Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0, 0, queue, &block)
      end

      # Returns a Dispatch::Source::DATA_OR
      def on_or(queue = Dispatch::Queue.concurrent, &block)
        Dispatch::Source.new(Dispatch::Source::DATA_OR, 0, 0, queue, &block)
      end

      # Takes events: :delete, :write, :extend, :attrib, :link, :rename, :revoke
      # Returns a Dispatch::Source::PROC
      def on_process_event(pid, events, queue = Dispatch::Queue.concurrent, &block)
        mask = events2mask(events, @@proc_events)
        Dispatch::Source.new(Dispatch::Source::PROC, pid, mask, queue, &block)
      end

      # Returns a Dispatch::Source::SIGNAL
      def on_signal(signal, queue = Dispatch::Queue.concurrent, &block)
        signal = Signal.list[signal.to_s] if signal.to_i == 0
        Dispatch::Source.new(Dispatch::Source::SIGNAL, signal, 0, queue, &block)
      end

      # Returns a Dispatch::Source::READ
      def on_read(file, queue = Dispatch::Queue.concurrent, &block)
        Dispatch::Source.new(Dispatch::Source::READ, file, 0, queue, &block)
      end

      # Returns a Dispatch::Source::WRITE
      def on_write(file, queue = Dispatch::Queue.concurrent, &block)
        Dispatch::Source.new(Dispatch::Source::WRITE, file, 0, queue, &block)
      end

      # Takes events: :exit, :fork, :exec, :signal
      # Returns a Dispatch::Source::VNODE
      def on_file_event(file, events, queue = Dispatch::Queue.concurrent, &block)
        mask = events2mask(events, @@vnode_events)
        Dispatch::Source.new(Dispatch::Source::VNODE, file, mask, queue, &block)
      end

      def on_interval(seconds, queue = Dispatch::Queue.concurrent, &block)
        Dispatch::Source.timer(0, seconds, 0, queue, &block)
      end
    end
  end
end
