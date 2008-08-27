HotCocoa::Mappings.map :web_view => :WebView , :framework => :WebKit do

  defaults :layout => {}, :frame => DefaultEmptyRect

  def init_with_options(web_view, options)
    web_view.initWithFrame(options.delete(:frame))
  end

  custom_methods do
    
    def url=(url)
      url = url.kind_of?(String) ? NSURL.alloc.initWithString(url) : url
      mainFrame.loadRequest(NSURLRequest.requestWithURL(url))
    end

    def auto_size
      setAutoresizingMask(NSViewHeightSizable|NSViewWidthSizable)
    end
    
  end

end