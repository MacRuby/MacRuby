require "stringio"

class RackURLProtocol < NSURLProtocol
  class << self
    def apps
      @apps ||= {}
    end

    def register(scheme, withRackApplication: app)
      NSURLProtocol.registerClass(self) if apps.empty?
      apps[scheme] = app
    end

    def unregister(scheme)
      apps.delete(scheme)
      NSURLProtocol.unregisterClass(self) if apps.empty?
    end

    def canInitWithRequest(request)
      apps.include?(request.URL.scheme)
    end

    def canonicalRequestForRequest(request)
      request
    end
  end

  def startLoading
    app = self.class.apps[request.URL.scheme]
    env = {
      "REQUEST_METHOD"    => request.HTTPMethod,
      "SCRIPT_NAME"       => "",
      "PATH_INFO"         => request.URL.path,
      "QUERY_STRING"      => request.URL.query,
      "SERVER_NAME"       => "localhost",
      "SERVER_PORT"       => "0",
      "rack.version"      => [1, 0],
      "rack.url_scheme"   => "http",
      "rack.input"        => inputStreamFromRequest,
      "rack.errors"       => $stderr,
      "rack.multithread"  => false,
      "rack.multiprocess" => false,
      "rack.run_once"     => false
    }

    status, headers, parts = app.call(env)
    data = dataFromRackResponseParts(parts)

    response = NSURLResponse.alloc.initWithURL(request.URL, MIMEType: "text/html", expectedContentLength: data.length, textEncodingName: nil)
    client.URLProtocol(self, didReceiveResponse: response, cacheStoragePolicy: NSURLCacheStorageNotAllowed)

    client.URLProtocol(self, didLoadData: data)
    client.URLProtocolDidFinishLoading(self)
  end

  def stopLoading
  end

  private
    def inputStreamFromRequest
      if body = request.HTTPBody
        input = NSString.alloc.initWithData(body, encoding: requestEncoding)
        StringIO.new(input)
      else
        StringIO.new
      end
    end

    def dataFromRackResponseParts(parts)
      body = ""
      parts.each { |part| body << part }
      body.dataUsingEncoding(requestEncoding)
    end

    def requestEncoding
      NSUTF8StringEncoding
    end
end
