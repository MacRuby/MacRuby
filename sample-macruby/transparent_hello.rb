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
    "Hello, Ruby Baby".drawAtPoint(NSPoint.new(0, 0), withAttributes:attributes)
  end
end

# First, to establish a connection to the window server,
# we must initialize the application
application = NSApplication.sharedApplication

# Create the window
frame = NSRect.new(NSPoint.new(0, 0), NSSize.new(450, 200))
window = NSWindow.alloc.initWithContentRect(frame, 
    styleMask:NSBorderlessWindowMask,
    backing:NSBackingStoreBuffered,
    defer:false)

# Allow the window to be partially transparent
window.setOpaque(false)

# Setup the window's root view
view = HelloView.alloc.initWithFrame(frame)
window.setContentView(view)

# Place the window near the top of the screen.
# (Screen coordinates in Cocoa are always PostScript
# coordinates, which start from the bottom of the screen
# and increase as they go up, so we have to do some math
# to place the window at 100 pixels from the top of the
# screen.
screenFrame = NSScreen.mainScreen.frame
windowOriginPoint = NSPoint.new(40, 
    screenFrame.origin.y + screenFrame.size.height - 100)
window.setFrameOrigin(windowOriginPoint)

# Show the window
window.makeKeyAndOrderFront(nil)
window.orderFrontRegardless

view.setNeedsDisplay(true)

# Prepare a timer that will auto-terminate our application
$stderr.puts "Starting. Application will automatically quit in 5 seconds."
NSTimer.scheduledTimerWithTimeInterval(5.0,
    target:application,
    selector:'terminate:',
    userInfo:nil,
    repeats:false)

# And start the application event loop
application.run
