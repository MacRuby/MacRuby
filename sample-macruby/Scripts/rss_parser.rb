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

framework 'Cocoa'
# Documentation for NSXMLParser delegates:
# http://developer.apple.com/mac/library/documentation/cocoa/reference/NSXMLParserDelegate_Protocol/Reference/Reference.html

class RSSParser
  attr_accessor :parser, :xml_url, :doc
  
  def initialize(xml_url)
    @xml_url = xml_url
    NSApplication.sharedApplication
    url = NSURL.alloc.initWithString(xml_url)
    @parser = NSXMLParser.alloc.initWithContentsOfURL(url)
    @parser.shouldProcessNamespaces = true
    @parser.delegate = self
    @items = []
  end
  
  # RSSItem is a simple class that holds all of RSS items.
  # Extend this class to display/process the item differently.
  class RSSItem
    attr_accessor :title, :description, :link, :guid, :pubDate, :enclosure
    def initialize
      @title, @description, @link, @pubDate, @guid = '', '', '', '', ''
    end
  end
  
  # Starts the parsing and send each parsed item through its block.
  #
  # Usage:
  #   feed.parse do |item|
  #     puts item.link
  #   end
  def parse(&block)
    @block = block
    puts "Parsing #{xml_url}"
    @parser.parse
  end
  
  # Starts the parsing but keep block the main runloop
  # until the parsing is done.
  # Do not use this method in a GUI app.
  # use #parse instead.
  def block_while_parsing(&block)
    @parsed = false
    parse(&block)
    NSRunLoop.currentRunLoop.runUntilDate(NSDate.distantFuture) until @parsed
  end
  
  # Delegate getting called when parsing starts
  def parserDidStartDocument(parser)
    puts "starting parsing.."
  end
  
  # Delegate being called when an element starts being processed
  def parser(parser, didStartElement:element, namespaceURI:uri, qualifiedName:name, attributes:attrs)
    if element == 'item'
      @current_item = RSSItem.new
    elsif element == 'enclosure'
      @current_item.enclosure = attrs
    end
    @current_element = element
  end
  
  # as the parser finds characters, this method is being called
  def parser(parser, foundCharacters:string)
    if @current_item && @current_item.respond_to?(@current_element)
      el = @current_item.send(@current_element) 
      el << string if el.respond_to?(:<<)
    end
  end
  
  # method called when an element is done being parsed
  def parser(parser, didEndElement:element, namespaceURI:uri, qualifiedName:name)
    if element == 'item'
      @items << @current_item
    end
  end
  
  # delegate getting called when the parsing is done
  # If a block was set, it will be called on each parsed items
  def parserDidEndDocument(parser)
    @parsed = true
    puts "done parsing"
    if @block
      @items.each{|item| @block.call(item)}
    end
  end
  
end


twitter = RSSParser.new("http://twitter.com/statuses/user_timeline/17093090.rss")

# because we are running in a script, we need the run loop to keep running
# until we are done with parsing
#
# If we would to use the above code in a GUI app,
# we would use #parse instead of #block_while_parsing
twitter.block_while_parsing do |item|
  print item.title
end