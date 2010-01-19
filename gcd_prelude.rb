module Dispatch
  class Source
    @@proc_events = {
       exit:PROC_EXIT,
       fork:PROC_FORK,
       exec:PROC_EXEC,
       signal:PROC_SIGNAL}
    def self.proc_event(e)
      (e.is_a? Numeric) ? e.to_i : @@proc_events[e]
    end

    @@vnode_events = {
      delete:VNODE_DELETE,
      write:VNODE_WRITE, 
      append:VNODE_EXTEND, 
      attrib:VNODE_ATTRIB, 
      link:VNODE_LINK, 
      rename:VNODE_RENAME, 
      revoke:VNODE_REVOKE}
    def self.vnode_event(e)
      (e.is_a? Numeric) ? e.to_i : @@vnode_events[e]
    end
  end
  
  class Queue
    def on_add(&block)
      Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0, 0, self, &block)
    end

    def on_or(&block)
      Dispatch::Source.new(Dispatch::Source::DATA_OR, 0, 0, self, &block)
    end

    def on_process_event(pid, *events, &block)
      mask = events.collect {|e| Dispatch::Source::proc_event(x)}.reduce(:|)
      Dispatch::Source.new(Dispatch::Source::DATA_PROC, pid, mask, self, &block)
    end

    def on_signal(signal, &block)
      signal = Signal.list[signal.to_s] if signal.to_i == 0
      Dispatch::Source.new(Dispatch::Source::DATA_SIGNAL, signal, 0, self, &block)
    end

    def on_read(file, &block)
      Dispatch::Source.new(Dispatch::Source::DATA_READ, file, 0, self, &block)
    end

    def on_write(file, &block)
      Dispatch::Source.new(Dispatch::Source::DATA_WRITE, file, 0, self, &block)
    end

    def on_file_event(file, *events, &block)
      mask = events.collect {|e| Dispatch::Source::vnode_event(x)}.reduce(:|)
      Dispatch::Source.new(Dispatch::Source::DATA_VNODE, file,mask, self,&block)
    end

    def on_interval(sec, &block)
      Dispatch::Source.timer(0, sec, 0, self, &block)
    end
  end
end
