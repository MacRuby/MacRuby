#!/usr/bin/env ruby
#
# DarkRoom
# Takes fullsize screenshots of a web page.
# Copyright (c) 2007 Justin Palmer.
#
# Released under an MIT LICENSE
#
# Usage
# ====
# ruby ./darkroom.rb http://activereload.net
# ruby ./darkroom.rb --output=google.png http://google.com
# ruby ./darkroom.rb --width=400 --delay=5 http://yahoo.com
#
# As a fix for the current bug specify a height:
# macruby darkroom.rb --output=google.png --height=600 http://google.com
require 'optparse'
require 'osx/cocoa'
OSX.require_framework 'Webkit'

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
        options[:website] = ARGV.first || 'http://google.com'
        Camera.shoot(options)
      end
    end
    
    class Camera
      def self.shoot(options)
        app = OSX::NSApplication.sharedApplication
        delegate = Processor.alloc.init!
        delegate.options = options
        app.setDelegate(delegate)
        app.run
      end
    end
    
    class Processor < OSX::NSObject
      include OSX
      attr_accessor :options, :web_view
      
      # def initialize
      #   #puts 'inits'
      #   rect = [-16000.0, -16000.0, 100, 100]
      #   win = NSWindow.alloc.initWithContentRect_styleMask_backing_defer(rect, NSBorderlessWindowMask, 2, 0)
      #   
      #   @web_view = WebView.alloc.initWithFrame(rect)
      #   @web_view.mainFrame.frameView.setAllowsScrolling(false)
      #   @web_view.setApplicationNameForUserAgent(USER_AGENT)
      #   @web_view.setFrameLoadDelegate(self)
      #   
      #   win.setContentView(@web_view)
      # end
      
      def init!
        if init
          rect = NSRect.new(NSSize.new(-16000.0, -16000.0), NSSize.new(100, 100))
          win = NSWindow.alloc.initWithContentRect_styleMask_backing_defer(rect, OSX::NSBorderlessWindowMask, 2, 0)
          
          @web_view = WebView.alloc.initWithFrame(rect)
          @web_view.mainFrame.frameView.setAllowsScrolling(false)
          @web_view.setApplicationNameForUserAgent(USER_AGENT)
          @web_view.setFrameLoadDelegate(self)
          win.setContentView(@web_view)
          
          self
        end
      end
      
      def applicationDidFinishLaunching(notification)
        @options[:output] ||= "#{Time.now.strftime('%m-%d-%y-%H%I%S')}.png"
        @web_view.window.setContentSize(NSSize.new(@options[:width], @options[:height]))
        @web_view.setFrameSize(NSSize.new(@options[:width], @options[:height]))
        @web_view.mainFrame.loadRequest(NSURLRequest.requestWithURL(NSURL.URLWithString(@options[:website])))
      end
      
      def webView_didFinishLoadForFrame(web_view, frame)
        viewport = web_view.mainFrame.frameView.documentView
        viewport.window.orderFront(nil)
        viewport.window.display
        viewport.window.setContentSize(NSSize.new(@options[:width], (@options[:height] > 0 ? @options[:height] : viewport.bounds.height)))
        viewport.setFrame(viewport.bounds)
        sleep(@options[:delay]) if @options[:delay]
        capture_and_save(viewport)
      end
      
      def capture_and_save(view)
        view.lockFocus
          bitmap = NSBitmapImageRep.alloc.initWithFocusedViewRect(view.bounds)
        view.unlockFocus
        
        bitmap.representationUsingType_properties(OSX::NSPNGFileType, nil).writeToFile_atomically(@options[:output], true)
        NSApplication.sharedApplication.terminate(nil)
      end
    end
  end
end
ActiveReload::DarkRoom::Photographer.new
