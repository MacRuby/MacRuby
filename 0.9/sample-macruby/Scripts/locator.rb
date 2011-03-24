#!/usr/local/bin/macruby

# Copyright (c) 2010 Matt Aimonetti
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

# Example script that finds your location and opens it on Google map page.
# Requires MacRuby 0.7.1 or newer

framework 'Cocoa'
framework 'CoreLocation'

# CLLocationManager wrapper
# more info on the CoreLocation:
# http://developer.apple.com/library/ios/#documentation/CoreLocation/Reference/CLLocationManager_Class/CLLocationManager/CLLocationManager.html
class LocationManager
    
  def initialize(&block)
    @loc          = CLLocationManager.alloc.init
    @loc.delegate = self
    @callback     = block
  end
  
  def start
    @loc.startUpdatingLocation
  end
  
  def stop
    @loc.stopUpdatingLocation
  end
  
  # Dispatch the CLLocationManager callback to the Ruby callback
  def locationManager(manager, didUpdateToLocation: new_location, fromLocation: old_location)
    @callback.call(new_location, self)
  end
  
end

location_manager = LocationManager.new do |new_location, manager|
  manager.stop
  puts "location: #{new_location.description}"
  url_string = "http://maps.google.com/maps?f=q&source=s_q&hl=en&geocode=&q=#{new_location.coordinate.latitude},#{new_location.coordinate.longitude}"
  url = NSURL.URLWithString(url_string)
  NSWorkspace.sharedWorkspace.openURL(url)
  exit
end

location_manager.start
NSRunLoop.currentRunLoop.runUntilDate(NSDate.distantFuture)