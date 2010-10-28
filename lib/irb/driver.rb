module IRB
  module Driver
    class << self
      def current=(driver)
        ThreadGroup.new.add(Thread.current)
        Thread.current[:irb_driver] = driver
      end
      
      def current
        current_thread = Thread.current
        current_thread[:irb_driver] ||= begin
          if group = current_thread.group
            driver = nil
            group.list.each do |thread|
              break if driver = thread[:irb_driver]
            end
            driver
          end
        end
      end

      def redirect_output!(redirector = OutputRedirector.new)
        before, $stdout = $stdout, redirector unless $stdout.is_a?(redirector.class)
        yield
      ensure
        $stdout = before if before
      end
    end
    
    class OutputRedirector
      def self.target
        if driver = IRB::Driver.current
          driver.output
        else
          $stderr
        end
      end
      
      # A standard output object has only one mandatory method: write.
      # It returns the number of characters written
      def write(object)
        string = object.respond_to?(:to_str) ? object : object.to_s
        send_to_target :write, string
        string.length
      end
      
      # if puts is not there, Ruby will automatically use the write
      # method when calling Kernel#puts, but defining it has 2 advantages:
      # - if puts is not defined, you cannot of course use $stdout.puts directly
      # - (objc) when Ruby emulates puts, it calls write twice
      #   (once for the string and once for the carriage return)
      #   but here we send the calls to another thread so it's nice
      #   to be able to save up one (slow) interthread call
      def puts(*args)
        send_to_target :puts, *args
        nil
      end
      
      # Override this if for your situation you need to dispatch from a thread
      # in a special manner.
      #
      # TODO: for macruby send to main thread
      def send_to_target(method, *args)
        self.class.target.__send__(method, *args)
      end
    end
  end
end
