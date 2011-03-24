#!/usr/local/bin/macruby

# Copyright (c) 2009 Matt Aimonetti
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# example script that fetches the top 5 results from google and capture the pages in PDF format.
# inspired by http://github.com/tomafro/macruby-snapper
#
# usage: 
# $ ./search_to_pdf.rb "Matt Aimonetti"

framework 'Cocoa'
framework 'WebKit'

require 'json'
require 'net/http'
require 'uri'
require 'cgi'

class Application

  def initialize
    NSApplication.sharedApplication.delegate = self
  end
  
  def urls_for(keyword)
    search_url = "http://ajax.googleapis.com/ajax/services/search/web?v=1.0&maxResults=10&q=#{CGI.escape(keyword)}"
    raw_results = Net::HTTP.get(URI.parse(search_url))
    results = JSON.parse(raw_results)
    results['responseData']['results'].map{|r| r['url']}
  end
  
  def capture(url)
    view = BrowserView.new
    puts "capturing #{url}"
    start_time = Time.now
    view.fetch(url)
    while !view.captured?
      if timed_out?(start_time)
        puts "Request timed out... moving on!"
        break 
      else
        NSRunLoop.currentRunLoop.runUntilDate(NSDate.distantFuture)
      end
    end
   end
   
   def timed_out?(start_time)
     (Time.now.to_i - start_time.to_i) > 30
   end

end

class BrowserView
  attr_accessor :view, :config, :url

  def initialize
    @view = WebView.alloc.initWithFrame([0, 0, 1024, 768])
    @captured = false
    window = NSWindow.alloc.initWithContentRect([0, 0, 1024, 768],
                                                styleMask:NSBorderlessWindowMask, 
                                                backing:NSBackingStoreBuffered, 
                                                defer:false)

    window.contentView = view
    # Use the screen stylesheet, rather than the print one.
    view.mediaStyle = 'screen'
    view.customUserAgent = 'Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; en-us) AppleWebKit/531.21.8 (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10'
    # Make sure we don't save any of the prefs that we change.
    view.preferences.autosaves = false
    # Set some useful options.
    view.preferences.shouldPrintBackgrounds = true
    view.preferences.javaScriptCanOpenWindowsAutomatically = false
    view.preferences.allowsAnimatedImages = false
    # Make sure we don't get a scroll bar.
    view.mainFrame.frameView.allowsScrolling = false
    view.frameLoadDelegate = self
  end
  
  def captured?
    @captured
  end

  def fetch(url)
    @url = url
    page_url = NSURL.URLWithString(url)
    view.mainFrame.loadRequest NSURLRequest.requestWithURL(page_url)
  end
  
  def webView(view, didFinishLoadForFrame:frame)
    save
  end
  
  def webView(view, didFailLoadWithError:error, forFrame:frame)
    puts "Failed to take snapshot: #{error.localizedDescription}"
    NSApplication.sharedApplication.terminate nil
  end

  def webView(view, didFailProvisionalLoadWithError:error, forFrame:frame)
    puts "Failed to take snapshot: #{error.localizedDescription}"
    NSApplication.sharedApplication.terminate nil
  end

  def save
    filename = url.gsub('http://', '').gsub('/', '-') + '.pdf'
    filepath = File.expand_path("#{File.dirname(__FILE__)}/#{filename}")
    puts "saving #{filepath}"
    docView = view.mainFrame.frameView.documentView
    width = docView.bounds.size.width
    height = docView.bounds.size.height
    docView.window.contentSize = [width, height]
    docView.frame = view.bounds
    docView.needsDisplay = true
    docView.displayIfNeeded
    docView.lockFocus
    docView.dataWithPDFInsideRect(docView.bounds).writeToFile(filepath, atomically:true)
    @captured = true
    docView.unlockFocus
  end

end

keyword = ARGV.shift || "MacRuby"
app = Application.new
app.urls_for(keyword).each do |url|
  app.capture(url)
end
NSApplication.sharedApplication.terminate(nil)