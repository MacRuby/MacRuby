require 'socket'

describe "Socket" do
  describe :sendfile do

    before :each do
      @port = 2112
      @server = TCPServer.open(@port)
      @server.listen(5)
      @socket = TCPSocket.open('localhost', @port)
    end

    after :each do
      @socket.close
      @server.close
    end
    
    it "should be able to send instances of IO" do
      io = File.open(File.dirname(__FILE__) + '/fixtures/sample.txt')
      
      Dispatch::Queue.concurrent.async do
        client = @server.accept
        client.sendfile(io, 0, io.stat.size)
        client.close
      end
      
      @socket.readlines.should == io.readlines
      io.close
    end
    
    it "should be able to send files specified by a path" do
      path = File.dirname(__FILE__) + '/fixtures/sample.txt'
      io = File.open(path)
      
      Dispatch::Queue.concurrent.async do
        client = @server.accept
        client.sendfile(path, 0, io.stat.size)
        client.close
      end
      
      @socket.readlines.should == io.readlines
      io.close
    end

   it "should raise if wrong value passed" do
      path = File.dirname(__FILE__) + '/fixtures/sample.txt'

      client = @server.accept
      lambda{ client.sendfile(Object.new, 0, 1) }.should raise_error(TypeError)
      lambda{ client.sendfile(path, Object.new, 1) }.should raise_error(TypeError)
      lambda{ client.sendfile(path, 0, Object.new) }.should raise_error(TypeError)
      lambda{ client.sendfile(path, -1, 1) }.should raise_error(ArgumentError)
      lambda{ client.sendfile(path, 0, -1) }.should raise_error(ArgumentError)
      client.close
    end

  end
end
