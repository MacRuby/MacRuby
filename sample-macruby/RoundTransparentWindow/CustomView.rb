# Description: 	This is the header file for the CustomView class, which handles the drawing of the window content.
#               we use a circle graphic and a pentagram graphic, switching between the two depending upon the 
#               window's transparency.

class CustomView < NSView

  # This method is called at app launch time when this class is unpacked from the nib.
  # We get set up here.
  def awakeFromNib
    # load the images we'll use from the bundle's Resources directory
    @circle_image = NSImage.imageNamed("circle")
    @penta_image  = NSImage.imageNamed("pentagram")
    # tell ourselves that we need displaying (force redraw)
    setNeedsDisplay(true)
  end

  # When it's time to draw, this method is called.
  # This view is inside the window, the window's opaqueness has been turned off,
  # and the window's styleMask has been set to NSBorderlessWindowMask on creation,
  # so what this view draws *is all the user sees of the window*.  The first two lines below
  # then fill things with "clear" color, so that any images we draw are the custom shape of the window,
  # for all practical purposes.  Furthermore, if the window's alphaValue is <1.0, drawing will use
  # transparency.
  def drawRect(rect)
    # erase whatever graphics were there before with clear
    NSColor.clearColor.set
    NSRectFill(frame)   
    # if our window transparency is >0.7, we decide to draw the circle.  Otherwise, draw the pentagram.
    # If we called -disolveToPoint:fraction: instead of -compositeToPoint:operation:, then the image
    # could itself be drawn with less than full opaqueness, but since we're already setting the alpha
    # on the entire window, we don't bother with that here.
    image_to_draw = (window.alphaValue > 0.7) ? @circle_image : @penta_image
    # same as `image_to_draw.compositeToPoint([0,0], operation:NSCompositeSourceOver)`
    # apart that compositeToPoint usage is now discouraged as the behavior it provides is not recommended 
    # for general use
    image_to_draw.drawAtPoint([0,0], fromRect:frame, operation:NSCompositeSourceOver, fraction:1.0)
    window.invalidateShadow
  end
  
end