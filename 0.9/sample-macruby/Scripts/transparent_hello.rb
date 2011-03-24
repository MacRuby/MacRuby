framework 'appkit'

class HelloView < NSView

  # When the Cocoa view system wants to draw a view,
  # it calls the method -(void)drawRect:(NSRect)rect.
  # The rectangle argument is relative to the origin
  # of the view's frame, and it may only be a small
  # portion of the view. For this reason, very
  # simple views with only one or two graphical
  # elements tend to ignore this parameter.
  def drawRect(rect)
 
    # Set the window background to transparent
    NSColor.clearColor.set
    NSRectFill(bounds)

    # Draw the text in a shade of red and in a large system font
    attributes = {
      NSForegroundColorAttributeName => NSColor.redColor,
      NSFontAttributeName => NSFont.boldSystemFontOfSize(48.0)
    }
    str = "Hello, Ruby Baby"
    str.drawAtPoint(NSPoint.new(0, 0), withAttributes:attributes)
  end
end

# First, to establish a connection to the window server,
# we must initialize the application
application = NSApplication.sharedApplication

# Create the window
frame = [0, 0, 450, 200]
window = NSWindow.alloc.initWithContentRect(frame,
    styleMask:NSBorderlessWindowMask,
    backing:NSBackingStoreBuffered,
    defer:false)

# Allow the window to be partially transparent
window.opaque = false

# Setup the window's root view
window.contentView = HelloView.alloc.initWithFrame(frame)

# Place the window near the top of the screen.
# (Screen coordinates in Cocoa are always PostScript
# coordinates, which start from the bottom of the screen
# and increase as they go up, so we have to do some math
# to place the window at 100 pixels from the top of the
# screen.
screenFrame = NSScreen.mainScreen.frame
window.frameOrigin = NSPoint.new(40, 
    screenFrame.origin.y + screenFrame.size.height - 100)

# Show the window
window.makeKeyAndOrderFront(nil)
window.orderFrontRegardless

# And start the application event loop
$stderr.puts "Starting. Press ^C to quit."
application.run
