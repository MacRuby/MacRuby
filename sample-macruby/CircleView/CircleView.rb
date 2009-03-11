
class CircleView < NSView

  # Many of the methods here are similar to those in the simpler DotView example.
  # See that example for detailed explanations; here we will discuss those
  # features that are unique to CircleView. 

  # CircleView draws text around a circle, using Cocoa's text system for
  # glyph generation and layout, then calculating the positions of glyphs
  # based on that layout, and using NSLayoutManager for drawing.

  def initWithFrame(frame)
    super(frame)

    # First, we set default values for the various parameters.
    @center = NSPoint.new
    @center.x = frame.size.width / 2.0
    @center.y = frame.size.height / 2.0
    @radius = 115.0
    @startingAngle = @angularVelocity = Math::PI / 2.0
    
    # Next, we create and initialize instances of the three 
    # basic non-view components of the text system:
    # an NSTextStorage, an NSLayoutManager, and an NSTextContainer.
    @textStorage = NSTextStorage.alloc.initWithString "Here's to the crazy ones, the misfits, the rebels, the troublemakers, the round pegs in the square holes, the ones who see things differently."
    @layoutManager = NSLayoutManager.new
    @textContainer = NSTextContainer.new
    @layoutManager.addTextContainer @textContainer
    @textStorage.addLayoutManager @layoutManager

    # Screen fonts are not suitable for scaled or rotated drawing.
    # Views that use NSLayoutManager directly for text drawing should
    # set this parameter appropriately.
    @layoutManager.usesScreenFonts = false

    # Returning a reference to self is mandatory in Cocoa initializers.
    return self
  end

  def drawRect(rect)    
    NSColor.whiteColor.set
    NSRectFill(bounds)

    # Note that usedRectForTextContainer: does not force layout, so it must 
    # be called after glyphRangeForTextContainer:, which does force layout.

    glyphRange = @layoutManager.glyphRangeForTextContainer @textContainer
    usedRect = @layoutManager.usedRectForTextContainer @textContainer
    context = NSGraphicsContext.currentContext

    glyphRange.location.upto(glyphRange.location + glyphRange.length - 1) do |i|
      lineFragmentRect = @layoutManager.lineFragmentRectForGlyphAtIndex i, effectiveRange:nil
      layoutLocation = @layoutManager.locationForGlyphAtIndex(i)
      transform = NSAffineTransform.transform
      
      # Here layoutLocation is the location (in container coordinates) where the glyph was laid out.
      layoutLocation.x += lineFragmentRect.origin.x
      layoutLocation.y += lineFragmentRect.origin.y
      
      # We then use the layoutLocation to calculate an appropriate position for the glyph
      # around the circle (by angle and distance, or viewLocation in rectangular coordinates).
      distance = @radius + usedRect.size.height - layoutLocation.y
      angle = @startingAngle + layoutLocation.x / distance
      
      viewLocation = NSPoint.new
      viewLocation.x = @center.x + distance * Math.sin(angle);
      viewLocation.y = @center.y + distance * Math.cos(angle);

      # We use a different affine transform for each glyph, to position and rotate it
      # based on its calculated position around the circle.  
      transform.translateXBy viewLocation.x, yBy:viewLocation.y
      transform.rotateByRadians(-angle)

      # We save and restore the graphics state so that the transform applies only to this glyph.
      context.saveGraphicsState
      transform.concat
      # drawGlyphsForGlyphRange: draws the glyph at its laid-out location in container coordinates.
      # Since we are using the transform to place the glyph, we subtract the laid-out location here.
      @layoutManager.drawGlyphsForGlyphRange NSRange.new(i, 1), atPoint:NSPoint.new(-layoutLocation.x, -layoutLocation.y)
      context.restoreGraphicsState
    end
  end
  
  def isOpaque
    true
  end
  
  # DotView changes location on mouse up, but here we choose to do so
  # on mouse down and mouse drags, so the text will follow the mouse.

  def mouseDown(event)
    centerWithEvent(event)
  end

  def mouseDragged(event)
    centerWithEvent(event)
  end
  
  def centerWithEvent(event)
    location = event.locationInWindow
    @center = convertPoint location, fromView:nil
    setNeedsDisplay true
  end

  # DotView uses action methods to set its parameters.  Here we have
  # factored each of those into a method to set each parameter directly
  # and a separate action method.

  def setColor(color)
    # Text drawing uses the attributes set on the text storage rather
    # than drawing context attributes like the current color.
    @textStorage.addAttribute NSForegroundColorAttributeName, value:color, range:NSRange.new(0, @textStorage.length)
    setNeedsDisplay true
  end

  def setRadius(distance)
    @radius = distance
    setNeedsDisplay true
  end

  def setStartingAngle(angle)
    @startingAngle = angle
    setNeedsDisplay true
  end
    
  def setAngularVelocity(velocity)
    @angularVelocity = velocity
    setNeedsDisplay true
  end
  
  def setString(string)
    @textStorage.replaceCharactersInRange(NSRange.new(0, @textStorage.length), withString:string)
    setNeedsDisplay true
  end    

  def takeColorFrom(sender)
    setColor sender.color
  end
  
  def takeRadiusFrom(sender)
    setRadius sender.doubleValue
  end

  def takeStartingAngleFrom(sender)
    setStartingAngle sender.doubleValue
  end

  def takeAngularVelocityFrom(sender)
    setAngularVelocity sender.doubleValue
  end
  
  def takeStringFrom(sender)
    setString sender.stringValue
  end

  def startAnimation(sender)
    stopAnimation sender
    
    # We schedule a timer for a desired 30fps animation rate.
    # In performAnimation: we determine exactly
    # how much time has elapsed and animate accordingly.
    @timer = NSTimer.scheduledTimerWithTimeInterval 1.0/30.0, 
      target:self,
      selector:'performAnimation:',
      userInfo:nil,
      repeats:true
           
    # The next two lines make sure that animation will continue to occur
    # while modal panels are displayed and while event tracking is taking
    # place (for example, while a slider is being dragged).
    NSRunLoop.currentRunLoop.addTimer @timer, forMode:NSModalPanelRunLoopMode
    NSRunLoop.currentRunLoop.addTimer @timer, forMode:NSEventTrackingRunLoopMode
    
    @lastTime = NSDate.timeIntervalSinceReferenceDate
  end

  def stopAnimation(sender)
    if @timer
      @timer.invalidate
      @timer = nil
    end
  end

  def toggleAnimation(sender)
    if @timer
      stopAnimation sender
    else
      startAnimation sender
    end
  end

  def performAnimation timer
    # We determine how much time has elapsed since the last animation,
    # and we advance the angle accordingly.
    thisTime = NSDate.timeIntervalSinceReferenceDate
    setStartingAngle @startingAngle + @angularVelocity * (thisTime - @lastTime)
    @lastTime = thisTime
  end

end