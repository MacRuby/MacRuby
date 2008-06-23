#
# written by Chris Thomas for the article of DDJ May 2002.
#

require 'osx/cocoa'

class HelloView < OSX::NSView
  #
  # When the Cocoa view system wants to draw a view,
  # it calls the method -(void)drawRect:(NSRect)rect.
  # The rectangle argument is relative to the origin
  # of the view's frame, and it may only be a small
  # portion of the view. For this reason, very
  # simple views with only one or two graphical
  # elements tend to ignore this parameter.
  #
  def drawRect(rect)
    
    # Set the window background to transparent
    OSX::NSColor.clearColor.set
    OSX::NSRectFill(bounds)
    
    # Draw the text in a shade of red and in a large system font
    attributes = OSX::NSMutableDictionary.alloc.init
    
    attributes.setObject_forKey(OSX::NSColor.redColor, OSX::NSForegroundColorAttributeName)
    attributes.setObject_forKey(OSX::NSFont.boldSystemFontOfSize(48.0), OSX::NSFontAttributeName)
    
    string = OSX::NSString.alloc.initWithString( "Hello, Ruby Baby" )
    string.drawAtPoint_withAttributes(OSX::NSSize.new(0,0), attributes)
    
    #
    # Turn the window's shadow off and on --
    # This is a kludge to get the shadow to recalculate
    # for the new shape of the opaque window content.
    #
    viewWindow = window
    window.setHasShadow(0)
    window.setHasShadow(1)
  end
end

#
# If this file is the main file, then perform the followjng commands.
# (This construct is often useful for adding simple unit tests to
# library code.)
#
if __FILE__ == $0
  #
  # First, to establish a connection to the window server,
  # we must initialize the application
  #
  $stderr.print "just wait ..." ; $stderr.flush
  application = OSX::NSApplication.sharedApplication
  
  # Create the window
  window = OSX::NSWindow.alloc.
    objc_send(:initWithContentRect, OSX::NSRect.new(OSX::NSSize.new(0, 0), OSX::NSSize.new(450, 200)),
                        :styleMask, OSX::NSBorderlessWindowMask,
                          :backing, OSX::NSBackingStoreBuffered,
                            :defer, 0)
  # Allow the window to be partially transparent
  window.setOpaque(0)
  
  # Setup the window's root view
  view = HelloView.alloc.initWithFrame(OSX::NSRect.new(OSX::NSSize.new(0, 0), OSX::NSSize.new(450, 200)))
  window.setContentView(view)
  
  # Place the window near the top of the screen.
  # (Screen coordinates in Cocoa are always PostScript
  # coordinates, which start from the bottom of the screen
  # and increase as they go up, so we have to do some math
  # to place the window at 100 pixels from the top of the
  # screen.
  #
  screenFrame = OSX::NSScreen.mainScreen.frame
  windowOriginPoint = OSX::NSSize.new(40, screenFrame.origin.y + screenFrame.size.height - 100)
  window.setFrameOrigin( windowOriginPoint )
  
  # Show the window
  window.makeKeyAndOrderFront(nil)
  window.orderFrontRegardless()    ## but this one does
  
  # And start the application event loop
  $stderr.print "\rtype `Ctrl-C' for quit !\n"
  trap('SIGINT') { $stderr.puts "bye." ; exit 0 }
  application.run
end
