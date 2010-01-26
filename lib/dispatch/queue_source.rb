module Dispatch
  class Source
    @@proc_events = {
       exit:PROC_EXIT,
       fork:PROC_FORK,
       exec:PROC_EXEC,
       signal:PROC_SIGNAL
       }
    def self.proc_event(e)
      convert_event(e, @@proc_events)
    end

    @@vnode_events = {
      delete:VNODE_DELETE,
      write:VNODE_WRITE, 
      extend:VNODE_EXTEND, 
      attrib:VNODE_ATTRIB, 
      link:VNODE_LINK, 
      rename:VNODE_RENAME, 
      revoke:VNODE_REVOKE
      }
    def self.vnode_event(e)
      convert_event(e, @@vnode_events)
    end
    
    def self.convert_event(e, hash)
      return e.to_i if e.is_a? Numeric
      value = hash[e]
      raise ArgumentError, "No event type #{e}" if value.nil?
      value
    end
  end
  
  class Queue
    # Returns a Dispatch::Source::DATA_ADD
    def on_add(&block)
      Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0, 0, self, &block)
    end

    # Returns a Dispatch::Source::DATA_OR
    def on_or(&block)
      Dispatch::Source.new(Dispatch::Source::DATA_OR, 0, 0, self, &block)
    end

    # Takes event symbols: :delete, :write, :extend, :attrib, :link, :rename, :revoke
    # Returns a Dispatch::Source::PROC
    def on_process_event(pid, *events, &block)
      mask = events.collect {|e| Dispatch::Source::proc_event(e)}.reduce(:|)
      Dispatch::Source.new(Dispatch::Source::PROC, pid, mask, self, &block)
    end

    # Returns a Dispatch::Source::SIGNAL
    def on_signal(signal, &block)
      signal = Signal.list[signal.to_s] if signal.to_i == 0
      Dispatch::Source.new(Dispatch::Source::SIGNAL, signal, 0, self, &block)
    end

    # Returns a Dispatch::Source::READ
    def on_read(file, &block)
      Dispatch::Source.new(Dispatch::Source::READ, file, 0, self, &block)
    end

    # Returns a Dispatch::Source::WRITE
    def on_write(file, &block)
      Dispatch::Source.new(Dispatch::Source::WRITE, file, 0, self, &block)
    end

    # Takes event symbols: :exit, :fork, :exec, :signal
    # Returns a Dispatch::Source::VNODE
    def on_file_event(file, *events, &block)
      mask = events.collect {|e| Dispatch::Source::vnode_event(e)}.reduce(:|)
      Dispatch::Source.new(Dispatch::Source::VNODE, file,mask, self,&block)
    end

    def on_interval(sec, &block)
      Dispatch::Source.timer(0, sec, 0, self, &block)
    end
  end
end
