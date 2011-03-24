require 'rack_url_protocol'

class ApplicationController
  attr_accessor :webView

  def awakeFromNib
    RackURLProtocol.register("rack", withRackApplication: self)
    webView.mainFrameURL = "rack:///"
  end

  def call(env)
    [200, {"Content-Type" => "text/html"}, ["<h1>Hello, MacRuby!</h1>"]]
  end
end
