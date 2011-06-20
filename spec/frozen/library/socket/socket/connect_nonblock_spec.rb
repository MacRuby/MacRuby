require File.expand_path('../../../../spec_helper', __FILE__)
require File.expand_path('../../fixtures/classes', __FILE__)

require 'socket'

describe "Socket#connect_nonblock" do
  before :each do
    @hostname = "127.0.0.1"
    @addr = Socket.sockaddr_in(SocketSpecs.port, @hostname)
    @socket = Socket.new(Socket::AF_INET, Socket::SOCK_STREAM, 0)
  end

  after :each do
    @socket.close
  end

  platform_is_not :freebsd do
    it "takes an encoded socket address and starts the connection to it" do
      lambda {
        @socket.connect_nonblock(@addr)
      }.should raise_error(Errno::EINPROGRESS)
    end
  end

  platform_is :freebsd do
    it "takes an encoded socket address and starts the connection to it" do
      lambda {
        begin
          @socket.connect_nonblock(@addr)
        rescue Errno::EINPROGRESS
          r = @socket.getsockopt(Socket::SOL_SOCKET, Socket::SO_ERROR)
          if r.int == Errno::ECONNREFUSED::Errno
            raise Errno::ECONNREFUSED.new
          end
        end
      }.should raise_error(Errno::ECONNREFUSED)
    end
  end

  it "connects the socket to the remote side" do
    ready = false
    thread = Thread.new do
      server = TCPServer.new(@hostname, SocketSpecs.port)
      ready = true
      conn = server.accept
      conn << "hello!"
      conn.close
      server.close
    end

    Thread.pass while (thread.status and thread.status != 'sleep') or !ready

    begin
      @socket.connect_nonblock(@addr)
    rescue Errno::EINPROGRESS
    end

    IO.select nil, [@socket]

    begin
      @socket.connect_nonblock(@addr)
    rescue Errno::EISCONN
      # Not all OS's use this errno, so we trap and ignore it
    end

    @socket.read(6).should == "hello!"
  end
end
