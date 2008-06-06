#!/usr/bin/env ruby
#
# DarkRoom
# Takes fullsize screenshots of a web page.
# Copyright (c) 2007 Justin Palmer.
# Rewrote for MacRuby by Laurent Sansonetti.
#
# Released under an MIT LICENSE
#
# Usage
# ====
# ruby ./darkroom.rb http://activereload.net
# ruby ./darkroom.rb --output=google.png http://google.com
# ruby ./darkroom.rb --width=400 --delay=5 http://yahoo.com
#

require 'optparse'

framework 'WebKit'

module ActiveReload
  module DarkRoom
    USER_AGENT = "DarkRoom/0.1"
    class Photographer
      def initialize
        options = {}
        opts = OptionParser.new do |opts|
          opts.banner = "Usage: #$0 [options] URL"
      
          opts.on('-w', '--width=[WIDTH]', Integer, 'Force width of the screenshot') do |v|
            options[:width] = v
          end
      
          opts.on('-h', '--height=[HEIGHT]', Integer, 'Force height of screenshot') do |v|
            options[:height] = v
          end
          
          opts.on('-o', '--output=[FILENAME]', String, 'Specify filename for saving') do |v|
            options[:output] = v
          end
          
          opts.on('-d', '--delay=[DELAY]', Integer, 'Delay in seconds to give web page assets time to load') do |v|
            options[:delay] = v
          end
      
          opts.on_tail('-h', '--help', 'Display this message and exit') do
            puts opts
            exit
          end
        end.parse!
        options[:width]  ||= 1024
        options[:height] ||= 0
        options[:website] = ARGV.first || 'http://ruby-lang.org'
        Camera.shoot(options)
      end
    end

    class Camera
      def self.shoot(options)
        app = NSApplication.sharedApplication
        delegate = Processor.new
        delegate.options = options
        app.delegate = delegate
        app.run
      end
    end

    class Processor
      attr_accessor :options, :web_view
  
      def initialize
        rect = [-16000.0, -16000.0, 100, 100]
        win = NSWindow.alloc.initWithContentRect rect, 
          styleMask:NSBorderlessWindowMask,
          backing:2,
          defer:0
    
        @web_view = WebView.alloc.initWithFrame rect
        @web_view.mainFrame.frameView.allowsScrolling = false
        @web_view.applicationNameForUserAgent = USER_AGENT
        @web_view.frameLoadDelegate = self
    
        win.contentView = @web_view
      end
      
      def applicationDidFinishLaunching(notification)
        @options[:output] ||= "#{Time.now.strftime('%m-%d-%y-%H%I%S')}.png"
        @web_view.window.contentSize = [@options[:width], @options[:height]]
        @web_view.frameSize = [@options[:width], @options[:height]]
        @web_view.mainFrame.loadRequest NSURLRequest.requestWithURL NSURL.URLWithString @options[:website]
      end
  
      def webView(web_view, didFinishLoadForFrame:frame)
        viewport = web_view.mainFrame.frameView.documentView
        viewport.window.orderFront(nil)
        viewport.window.display
        viewport.window.contentSize = [@options[:width], (@options[:height] > 0 ? @options[:height] : viewport.bounds.size.height)]
        viewport.frame = viewport.bounds
        sleep(@options[:delay]) if @options[:delay]
        capture_and_save viewport
      end
  
      def capture_and_save(view)
        view.lockFocus
          bitmap = NSBitmapImageRep.alloc.initWithFocusedViewRect view.bounds
        view.unlockFocus
    
        repr = bitmap.representationUsingType NSPNGFileType, properties:nil
        repr.writeToFile @options[:output], atomically:true
        NSApplication.sharedApplication.terminate nil
      end
    end
  end
end

ActiveReload::DarkRoom::Photographer.new
