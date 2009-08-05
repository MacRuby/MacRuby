# A very simple Web server written on top of Foundation's run loop and
# dispatching new requests to a pool of threads.

framework 'Foundation'

class HTTPMessage
  attr_accessor :code, :content_type, :content

  def data
    str = "HTTP/1.1 #{code} "
    str << code == 404 ? "Not Found\n" : "OK\n"

    str << "Date: #{Time.now}\n"
    str << "Server: #{__FILE__} (MacRuby, Mac OS X)\n"
    str << "Content-Length: #{@content.length}\n"
    str << "Keep-Alive: timeout=5, max=100\n"
    str << "Connection: Keep-Alive\n"
    str << "Content-Type: #{@content_type}"
    if content_type.start_with?('text')
      str << "; charset=utf-8"
    end
    str << "\n\n"

    data = nil
    if @content.is_a?(NSData)
      data = str.dataUsingEncoding(NSUTF8StringEncoding)
      data = data.mutableCopy
      data.appendData(@content)
    else
      str << @content
      data = str.dataUsingEncoding(NSUTF8StringEncoding)
    end
    data
  end
end

class WebServer
  def initialize(dir, port, debug)
    @debug = debug
    log "Preparing server to serve #{dir} on http://localhost:#{port}"

    @dir = dir
    @port = NSSocketPort.alloc.initWithTCPPort(port)
    unless @port
      raise "Can't open local TCP socket on port #{port}"
    end
    fd = @port.socket

    @handle = NSFileHandle.alloc.initWithFileDescriptor fd,
      closeOnDealloc:true
    
    NSNotificationCenter.defaultCenter.addObserver self,
      selector:'new_connection:',
      name:NSFileHandleConnectionAcceptedNotification,
      object:nil

    @handle.acceptConnectionInBackgroundAndNotify
  end

  THREADS_POOL = 4

  def run
    log "Starting server"

    @threads = []
    @work_queue = []
    @work_mutex = Mutex.new
    THREADS_POOL.times do
      @threads << Thread.new do
        begin
          loop do
            sleep 0.1
            handle = @work_mutex.synchronize { @work_queue.shift }
            if handle
              handle_request(handle)
            end
          end
        rescue => e
          puts "ERROR: #{e}"
        end
      end
    end

    NSRunLoop.currentRunLoop.run
  end

  def new_connection(notification)
    dict = notification.userInfo
    remote_handle = dict[NSFileHandleNotificationFileHandleItem]
 
    @handle.acceptConnectionInBackgroundAndNotify

    if remote_handle
      @work_mutex.synchronize { @work_queue << remote_handle }
      @threads.each { |t| t.wakeup }
    end
  end

  private

  def handle_request(handle)
     data = handle.availableData
     str = NSString.alloc.initWithData(data, encoding:NSUTF8StringEncoding)
     if str.start_with?('GET ')
       i = str.index(' ', 4)
       path = str[4..(i-1)]
       data = serve_data(path)
       handle.writeData(data)
     end
     handle.closeFile
  end

  def serve_file(msg, path, real_path)
    data = NSData.dataWithContentsOfFile(real_path)
    msg.content = data
    msg.content_type = case File.extname(real_path)
      when '.gif'
        'image/gif'
      when '.png'
        'image/png'
      when '.jpg', '.jpeg'
        'image/jpg'
      when '.html'
        'text/html'
      else
        'text/plain'
    end
  end

  def serve_directory(msg, path, real_path)
    str = <<EOS
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2 Final//EN">
<html>
  <head>
    <title>Index of #{path}</title>
  </head>
  <body>
  <h1>Index of #{path}</h1>
  <pre>
EOS
    Dir.entries(real_path).each do |entry|
      entry_path = File.join(path, entry)
      str << "  <a href=\"#{entry_path}\">#{entry_path}</a>\n"
    end
    str << <<EOS
  <hr>
  </pre>
</body>
</html>
EOS
    msg.content_type = 'text/html'
    msg.content = str
  end

  def serve_data(path)
    real_path = File.join(@dir, path)
    msg = HTTPMessage.new

    if File.exist?(real_path)
      msg.code = 200
      if File.directory?(real_path)
        serve_directory(msg, path, real_path)
      else
        serve_file(msg, path, real_path)
      end
    else
      msg.code = 404
      msg.content_type = 'text/html'
      msg.content = <<EOS
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML 2.0//EN">
<html>
  <head>
    <title>404 Not Found</title>
  </head>
  <body>
    <h1>Not Found</h1>
    <p>The requested URL #{path} was not found on this server.</p>
  </body>
</html>
EOS
    end

    log "#{msg.code} #{real_path}"
    msg.data
  end

  def log(msg)
    puts "#{Thread.current} LOG: #{msg}" if @debug
  end
end

dir, port, debug = ARGV[0], ARGV[1], ARGV[2]

dir ||= '/'
port ||= 8080
debug ||= false

server = WebServer.new(dir, port, debug)
server.run
