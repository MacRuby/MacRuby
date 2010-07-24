require 'irb/driver/tty'
require 'socket'

module IRB
  module Driver
    class Socket
      class << self
        attr_reader :instance
        
        def run(object, binding)
          @instance = new(object, binding)
          @instance.run
        end
      end
      
      # Initializes with the object and binding that each new connection will
      # get as Context. The binding is shared, so local variables will stay
      # around. The benefit of this is that a socket based irb session is most
      # probably used to debug a running application in development. In this
      # scenario it could be beneficial to keep local vars in between sessions.
      #
      # TODO see if that actually works out ok.
      def initialize(object, binding, host = '127.0.0.1', port = 7829)
        @object, @binding = object, binding
        @host, @port = host, port
        @server = TCPServer.new(host, port)
      end
      
      # TODO libedit doesn't use the right input and output, so we can't use Readline for now!!
      def run
        $stderr.puts "[!] Running IRB server on #{@host}:#{@port}"
        loop do
          connection = @server.accept
          Thread.new do
            # assign driver with connection to current thread and start runloop
            IRB::Driver.current = TTY.new(connection, connection)
            irb(@object, @binding)
            connection.close
          end
        end
      end
    end
  end
end

module Kernel
  alias_method :irb_before_socket, :irb
  
  def irb(object, binding = nil)
    if IRB::Driver::Socket.instance.nil?
      IRB::Driver::Socket.run(object, binding)
    else
      irb_before_socket(object, binding)
    end
  end
  
  private :irb, :irb_before_socket
end