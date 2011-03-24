class PackerView < NSView
  
  BUTTON_MARGIN = 4.0

  def HalfX(r); r.origin.x + r.size.width * 0.5; end
  def QuarterY(r); r.origin.y + r.size.height * 0.25; end
  def HalfY(r); r.origin.y + r.size.height * 0.5; end
  def ThreeQuarterY(r); r.origin.y + r.size.height * 0.75; end
  def leftSide?(pageNum); pageNum == 0 or pageNum > 4; end

  NumberAttributes = {
    NSFontAttributeName => NSFont.fontWithName('Helvetica', size:40.0),
    NSForegroundColorAttributeName => NSColor.blueColor
  }

  def clearPage(sender)
    @packModel.removeImageRepAtPage sender.tag
  end

  def placeButtons
    subviews.dup.each { |v| v.removeFromSuperviewWithoutNeedingDisplay }
    BLOCK_COUNT.times do |i|
      if @packModel.pageIsFilled(i)
        fullRect = fullRectForPage(i)
        buttonRect = NSRect.new
        buttonRect.size = NSMakeSize(30, 25)
        buttonRect.origin.x = NSMaxX(fullRect) - 30 - BUTTON_MARGIN
        buttonRect.origin.y = NSMinY(fullRect) + BUTTON_MARGIN        
        button = NSButton.alloc.initWithFrame(buttonRect)
        button.cell = RoundCloseButtonCell.new
        button.tag = i            
        button.target = self
        button.action = 'clearPage:'
        addSubview(button)
      end
    end
  end

  def initWithFrame(frameRect)
    if super
      @imageablePageRect = NSInsetRect(bounds, 15.0, 15.0)
      registerForDraggedTypes [NSPDFPboardType, NSFilenamesPboardType, nil]
      @dropPage = -1
      @dragStart = -1
      
      NSNotificationCenter.defaultCenter.addObserver self,
        selector:'paperSizeChanged:',
        name:PaperSizeChangedNotification,
        object:nil
      
      prepareBezierPaths

      bFrame = NSRect.new(NSZeroPoint, NSSize.new(20, 20))
      b = NSButton.alloc.initWithFrame bFrame
      b.cell = RoundCloseButtonCell.alloc.init
      addSubview(b)
      self
    end
  end

  def correctWindowForChangeFromSize(oldSize, toSize:newSize)
    frame = window.frame
    frame.size.width += newSize.width - oldSize.width
    frame.size.height += newSize.height - oldSize.height
    window.setFrame frame, display:true    
  end

  def updateSize
    oldSize = self.frame.size
    newSize = PreferenceController.sharedPreferenceController.paperSize
    setFrameSize(newSize)
    correctWindowForChangeFromSize(oldSize, toSize:newSize)
    @imageablePageRect = NSInsetRect(bounds, 24.0, 24.0)
    prepareBezierPaths
    superview.setNeedsDisplay true
  end

  def awakeFromNib
    updateSize
  end

  def setPackModel(pm)
    if @packModel != pm
      nc = NSNotificationCenter.defaultCenter
      if @packModel
        nc.removeObserver self, name:PackModelChangedNotification, object:@packModel
      end
      @packModel = pm
      nc.addObserver self, selector:'modelChanged:', name:PackModelChangedNotification, object:@packModel
      placeButtons
      setNeedsDisplay true
    end
  end

  attr_reader :packModel
  
  def changeImageableRectDisplay(sender)
    @showsImageableRect = sender.state == 1
    setNeedsDisplay true
  end

  def modelChanged(note)
    placeButtons
    setNeedsDisplay true
  end

  def paperSizeChanged(note)
    updateSize
    placeButtons
  end

  def drawNumber(x, centeredInRect:r)
    str = x.to_s
    attString = NSAttributedString.alloc.initWithString str, attributes:NumberAttributes
    drawingRect = NSRect.new
    drawingRect.size = attString.size
    drawingRect.origin.x = r.origin.x + (r.size.width - drawingRect.size.width) / 2.0
    drawingRect.origin.y = r.origin.y + (r.size.height - drawingRect.size.height) / 2.0
    attString.drawInRect drawingRect
  end

  def prepareBezierPaths
    rect = bounds
    left = NSMinX(rect)
    right = NSMaxX(rect)
    top = NSMaxY(rect)
    bottom = NSMinY(rect)
    lowerH = QuarterY(rect)
    midH = HalfY(rect)
    upperH = ThreeQuarterY(rect)
    midV = HalfX(rect)

    @foldLines = NSBezierPath.new
    @foldLines.lineWidth = 1.0
    @foldLines.moveToPoint NSMakePoint(left, lowerH)
    @foldLines.lineToPoint NSMakePoint(right, lowerH)
    @foldLines.moveToPoint NSMakePoint(left, midH)
    @foldLines.lineToPoint NSMakePoint(right, midH)
    @foldLines.moveToPoint NSMakePoint(left, upperH)
    @foldLines.lineToPoint NSMakePoint(right, upperH)
    @foldLines.moveToPoint NSMakePoint(midV, top)
    @foldLines.lineToPoint NSMakePoint(midV, upperH)
    @foldLines.moveToPoint NSMakePoint(midV, lowerH)
    @foldLines.lineToPoint NSMakePoint(midV, bottom)
        
    @cutLine = NSBezierPath.new
    p = Pointer.new(:double, 2)
    p[0] = 7.0
    p[1] = 3.0
    @cutLine.setLineDash p, count:2, phase:0
    @cutLine.moveToPoint NSMakePoint(midV, upperH)
    @cutLine.lineToPoint NSMakePoint(midV, lowerH)
    @cutLine.lineWidth = 1.0
  end

  def acceptsFirstMouse(event); true; end

  def drawRect(rect)
    isScreen = NSGraphicsContext.currentContextDrawingToScreen
    
    if isScreen
      NSColor.whiteColor.set
      NSBezierPath.fillRect rect
    end
    
    BLOCK_COUNT.times do |i|
      imageDest = imageableRectForPage(i)
      
      next unless NSIntersectsRect(imageDest, rect)
            
      imageRep = @packModel.preparedImageRepForPage(i)
      if imageRep
        drawImageRep imageRep, inRect:imageDest, isLeft:leftSide?(i)
      end                  

      if isScreen
        if i == @dragStart
          highlightRect = NSInsetRect(imageDest, 20, 20)
          c = NSColor.colorWithCalibratedRed 1, green:1, blue:0.5, alpha:0.5
          c.set
          NSBezierPath.fillRect highlightRect
        end
        drawNumber i+1, centeredInRect:imageDest
      end
    end

    NSColor.lightGrayColor.set
    @foldLines.stroke
    
    NSColor.blackColor.set
    @cutLine.stroke

    if isScreen
      if @showsImageableRect
        NSColor.blueColor.set
        NSBezierPath.defaultLineWidth = 1.0
        NSBezierPath.strokeRect @imageablePageRect
      end

      if @dropPage >= 0
        dropColor = NSColor.colorWithDeviceRed 0.8, green:0.5, blue:0.5, alpha:0.3
        dropColor.set
        NSBezierPath.fillRect fullRectForPage @dropPage
      end
    end
  end

  def drawImageRep(rep, inRect:rect, isLeft:isLeft)
    imageSize = rep.size
    isPortrait = imageSize.height > imageSize.width

    # Figure out the rotation (as a multiple of 90 degrees)
    rotation = isLeft ? 1 : -1
    rotation += 1 unless isPortrait
    
    # Figure out the scale
    scaleVertical = scaleHorizontal = 0.0
    # Is it rotated +/- 90 degrees?
    if rotation % 2
      scaleVertical = rect.size.height / imageSize.width
      scaleHorizontal = rect.size.width / imageSize.height
    else
      scaleVertical = rect.size.height / imageSize.height
      scaleHorizontal = rect.size.width / imageSize.width
    end
    
    scale = 0        # How much the image will be scaled
    widthGap = 0;    # How much it will need to be nudged to center horizontally
    heightGap = 0    # How much it will need to be nudged to center vertically
    if scaleHorizontal > scaleVertical
      scale = scaleVertical
      heightGap = 0
      widthGap = 0.5 * rect.size.width * (scaleHorizontal - scaleVertical) / scaleHorizontal
    else
      scale = scaleHorizontal
      widthGap = 0
      heightGap = 0.5 * rect.size.height * (scaleVertical - scaleHorizontal) / scaleVertical
    end
    
    origin = NSPoint.new
    case rotation
    when -1 
      origin.x = rect.origin.x + widthGap
      origin.y = rect.origin.y + rect.size.height - heightGap
    when 0
      origin.x = rect.origin.x + widthGap
      origin.y = rect.origin.y + heightGap
    when 1
      origin.x = rect.origin.x + rect.size.width - widthGap
      origin.y = rect.origin.y + heightGap
    when 2
      origin.x = rect.origin.x + rect.size.width - widthGap
      origin.y = rect.origin.y + rect.size.height - heightGap
    else
      raise "Rotation = #{rotation}?"
    end
    
    # Create the affine transform
    transform = NSAffineTransform.new
    transform.translateXBy origin.x, yBy:origin.y
    transform.rotateByDegrees rotation * 90.0
    transform.scaleBy scale
    NSGraphicsContext.saveGraphicsState
    NSRectClip(rect)
    transform.concat
    rep.draw
    NSGraphicsContext.restoreGraphicsState
  end

  def fullRectForPage(pageNum)
    result = NSRect.new
    bounds = self.bounds
    result.size = NSMakeSize(bounds.size.width * 0.5, bounds.size.height * 0.25)
    if leftSide?(pageNum)
      result.origin.x = NSMinX(bounds)
    else
      result.origin.x = HalfX(bounds)
    end
    case pageNum
    when 0, 1
      result.origin.y = ThreeQuarterY(bounds)
    when 2, 7
      result.origin.y = HalfY(bounds)
    when 3, 6
      result.origin.y = QuarterY(bounds)
    else
      result.origin.y = NSMinY(bounds)
    end
    result
  end

  def imageableRectForPage(pageNum)
    NSIntersectionRect(fullRectForPage(pageNum), @imageablePageRect)
  end

  def setImageablePageRect(r)
    if @imageablePageRect != r
      @imageablePageRect = r
      setNeedsDisplay true
    end
  end

  def setDragStart(i)
    if @dragStart != i
      if @dragStart != -1
        setNeedsDisplayInRect fullRectForPage(@dragStart)
      end      
      @dragStart = i
      if @dragStart != -1
        setNeedsDisplayInRect fullRectForPage(@dragStart)
      end
    end
  end

  def mouseDown(e)
    i = pageForPointInWindow e.locationInWindow
    if @packModel.pageIsFilled(i)
      setDragStart i
    end
  end

  def mouseDragged(e)
    if @dragStart != -1
      i = pageForPointInWindow e.locationInWindow
      if i != @dragStart
        setDropPage i
      end
    end
  end

  def mouseUp(e)
    if @dragStart != -1 and @dropPage != -1 and @dragStart != @dropPage
      mask = e.modifierFlags
      if mask & NSAlternateKeyMask
        @packModel.copyImageRepAt @dragStart, toRepAt:@dropPage
      else
        @packModel.swapImageRepAt @dragStart, withRepAt:@dropPage
      end
    end
    setDragStart -1
    setDropPage -1
  end

  def setDropPage(i)
    if i != @dropPage
      if @dropPage != -1
        setNeedsDisplayInRect fullRectForPage @dropPage
      end
      @dropPage = i
      if @dropPage != -1
        setNeedsDisplayInRect fullRectForPage @dropPage
      end
    end
  end

  def pageForPoint(p)
    bounds = self.bounds
    if NSPointInRect(p, bounds)
      BLOCK_COUNT.times do |i|
        r = fullRectForPage i
        if NSPointInRect(p, r)
          return i
        end
      end
    end
    -1
  end

  def pageForPointInWindow(p)
    x = convertPoint p, fromView:nil
    pageForPoint x
  end

  def draggingEntered(sender)
    p = sender.draggingLocation
    setDropPage pageForPointInWindow(p)
    NSDragOperationCopy
  end

  def draggingUpdated(sender)
    p = sender.draggingLocation
    setDropPage pageForPointInWindow(p)
    NSDragOperationCopy
  end

  def draggingExited(sender)
    setDropPage -1
  end

  def prepareForDragOperation(sender)
    true
  end

  def performDragOperation(sender)
    pasteboard = sender.draggingPasteboard
    favoriteTypes = [NSPDFPboardType, NSFilenamesPboardType]
    matchingType = pasteboard.availableTypeFromArray favoriteTypes
    if matchingType
      undo = @packModel.undoManager
      groupingLevel = undo.groupingLevel
    
      # This is an odd little hack.  Seems undo groups are not properly closed after drag 
      # from the finder.
      if groupingLevel > 0
        undo.endUndoGrouping
        undo.beginUndoGrouping
      end
    
      if matchingType == NSPDFPboardType
        d = pasteboard.dataForType matchingType
        @packModel.putPDFData d, startingOnPage:@dropPage
      end
    
      if matchingType == NSFilenamesPboardType
        filenames = pasteboard.propertyListForType matchingType
        @packModel.putFiles filenames, startingOnPage:@dropPage    
      end

      true
    else
      false
    end
  end

  def concludeDragOperation(sender)
    setDropPage -1
  end

  def knowsPageRange(rptr)
    rptr.assign NSRange.new(1, 1)
    true
  end

  def rectForPage(i)
    bounds
  end  
end