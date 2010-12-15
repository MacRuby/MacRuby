require 'socket'

describe "Socket" do
  describe :sendfile do
    
    it "should be able to send instances of IO" do
      port = 2112
      io = File.open(File.dirname(__FILE__) + '/fixtures/sample.txt')
      server = TCPServer.open(port)
      socket = TCPSocket.open('localhost', port)
      
      Dispatch::Queue.concurrent.async do
        server.listen(5)
        client = server.accept
        client.sendfile(io, 0, io.stat.size)
        client.close
      end
      
      socket.readlines.should == io.readlines
      socket.close
      server.close
    end
    
    it "should be able to send files specified by a path" do
      port = 2112
      path = File.dirname(__FILE__) + '/fixtures/sample.txt'
      io = File.open(path)
      server = TCPServer.open(port)
      socket = TCPSocket.open('localhost', port)
      
      Dispatch::Queue.concurrent.async do
        server.listen(5)
        client = server.accept
        client.sendfile(path, 0, io.stat.size)
        client.close
      end
      
      socket.readlines.should == io.readlines
      socket.close
      server.close
    end
  end
end
