# Description:  This is where we extend the windows behaviors to have a custom shape and be transparent.  
#               We also override the #mouseDown and #mouseDragged methods, to allow for dragging the window
#               by clicking on its content area (since it doesn't have a title bar to drag).

module CustomWindowBehaviors
  attr_accessor :initialLocation

  # We are extending initWithContentRect which is called by #window
  def initWithContentRect(contentRect, styleMask:aStyle, backing:bufferingType, defer:flag)
    # Call NSWindow's version of this function, but pass in the all-important value of NSBorderlessWindowMask
    #for the styleMask so that the window doesn't have a title bar
    result = super(contentRect, NSBorderlessWindowMask, NSBackingStoreBuffered, false)
    # Set the background color to clear so that (along with the setOpaque call below) we can see through the parts
    # of the window that we're not drawing into
    result.backgroundColor = color(:name => 'clear')
    # This next line pulls the window up to the front on top of other system windows.  This is how the Clock app behaves;
    # generally you wouldn't do this for windows unless you really wanted them to float above everything.
    result.level = NSStatusWindowLevel
    # Let's start with no transparency for all drawing into the window
    result.alphaValue = 1.0
    # but let's turn off opaqueness so that we can see through the parts of the window that we're not drawing into
    result.opaque = false
    # and while we're at it, make sure the window has a shadow, which will automatically be the shape of our custom content.
    result.hasShadow = true
    result
  end

  # Custom windows that use the NSBorderlessWindowMask can't become key by default.  Therefore, controls in such windows
  # won't ever be enabled by default. Thus, we override this method to change that.
  def canBecomeKeyWindow
    true
  end

  # Once the user starts dragging the mouse, we move the window with it. We do this because the window has no title
  # bar for the user to drag (so we have to implement dragging ourselves)
  # (overriding original method)
  def mouseDragged(theEvent)
    screen_frame = NSScreen.mainScreen.frame
    window_frame = self.frame
    current_location = self.convertBaseToScreen(self.mouseLocationOutsideOfEventStream)

    # grab the current global mouse location; we could just as easily get the mouse location 
    # in the same way as we do in -mouseDown:
    new_origin = NSPoint.new((current_location.x - @initialLocation.x), (current_location.y - @initialLocation.y))

    # Don't let the window get dragged up under the menu bar
    if((new_origin.y + window_frame.size.height) > (screen_frame.origin.y + screen_frame.size.height))
      new_origin.y = screen_frame.origin.y + (screen_frame.size.height - window_frame.size.height)
    end

    # go ahead and move the window to the new location
    self.frameOrigin = new_origin
  end

  # We start tracking the a drag operation here when the user first clicks the mouse,
  # to establish the initial location.
  def mouseDown(theEvent)
    window_frame = frame
    
    # grab the mouse location in global coordinates
    @initialLocation = convertBaseToScreen(theEvent.locationInWindow)
    @initialLocation.x -= window_frame.origin.x
    @initialLocation.y -= window_frame.origin.y
  end
  
  
end