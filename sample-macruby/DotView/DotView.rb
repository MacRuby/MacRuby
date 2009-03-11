class DotView < NSView
  
  # initWithFrame: is NSView's designated initializer (meaning it should be
  # overridden in the subclassers if needed, and it should call super, that is
  # NSView's implementation).  In DotView we do just that, and also set the
  # instance variables.
  #
  # Note that we initialize the instance variables here in the same way they are
  # initialized in the nib file. This is adequate, but a better solution is to make
  # sure the two places are initialized from the same place. Slightly more
  # sophisticated apps which load nibs for each document or window would initialize
  # UI elements at the time they're loaded from values in the program.

  def initWithFrame frame
    super(frame)
    @center = NSPoint.new(50.0, 50.0)
    @radius = 10.0
    @color = NSColor.redColor
    self
  end

  # drawRect: should be overridden in subclassers of NSView to do necessary
  # drawing in order to recreate the the look of the view. It will be called
  # to draw the whole view or parts of it (pay attention the rect argument);
  # it will also be called during printing if your app is set up to print.
  # In DotView we first clear the view to white, then draw the dot at its
  # current location and size.

  def drawRect rect
    NSColor.whiteColor.set
    NSRectFill(bounds) # Equiv to NSBezierPath.bezierPathWithRect(bounds).fill
  
    dotRect = NSRect.new
    dotRect.origin.x = @center.x - @radius
    dotRect.origin.y = @center.y - @radius
    dotRect.size.width  = 2 * @radius
    dotRect.size.height = 2 * @radius
    
    @color.set
    NSBezierPath.bezierPathWithOvalInRect(dotRect).fill
  end

  # Views which totally redraw their whole bounds without needing any of the
  # views behind it should override isOpaque to return true. This is a performance
  # optimization hint for the display subsystem. This applies to DotView, whose
  # drawRect: does fill the whole rect its given with a solid, opaque color.

  def isOpaque
    true
  end

  # Recommended way to handle events is to override NSResponder (superclass
  # of NSView) methods in the NSView subclass. One such method is mouseUp:.
  # These methods get the event as the argument. The event has the mouse
  # location in window coordinates; use convertPoint:fromView: (with "nil"
  # as the view argument) to convert this point to local view coordinates.
  #
  # Note that once we get the new center, we call setNeedsDisplay:YES to 
  # mark that the view needs to be redisplayed (which is done automatically
  # by the AppKit).

  def mouseUp event
    @center = convertPoint event.locationInWindow, fromView:nil
    setNeedsDisplay true
  end

  # setRadius: is an action method which lets you change the radius of the dot.
  # We assume the sender is a control capable of returning a floating point
  # number; so we ask for it's value, and mark the view as needing to be 
  # redisplayed. A possible optimization is to check to see if the old and
  # new value is the same, and not do anything if so.

  def setRadius(sender)
    @radius = sender.doubleValue
    setNeedsDisplay true
  end

  # setColor: is an action method which lets you change the color of the dot.
  # We assume the sender is a control capable of returning a color (NSColorWell
  # can do this). We get the value and mark the view as needing to be redisplayed. 
  # A possible optimization is to check to see if the old and new value is the same, 
  # and not do anything if so.
 
  def setColor(sender)
    @color = sender.color
    setNeedsDisplay true
  end
  
end