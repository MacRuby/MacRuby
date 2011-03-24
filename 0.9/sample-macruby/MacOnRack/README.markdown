MacOnRack Demo
==============

A sample MacRuby application demonstrating how to connect Rack to a WebView.

    require 'rack_url_protocol'

    class ApplicationController
      attr_accessor :webView

      def awakeFromNib
        # Register your custom scheme.
        # This should be unique to your app.
        RackURLProtocol.register("rack", withRackApplication: self)

        # Load the root "/" page
        webView.mainFrameURL = "rack:///"
      end

      def call(env)
        [200, {"Content-Type" => "text/html"}, ["<h1>Hello, MacRuby!</h1>"]]
      end
    end


Try it with Sinatra or any other rack application that runs on MacRuby.

See RackURLProtocol [sstephenson/rack_url_protocol](http://github.com/sstephenson/rack_url_protocol) for the
latest adapter.

Note: this example was written by Joshua Peek and is also available at http://github.com/josh/MacOnRack.
