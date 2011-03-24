class DraggingSourcePDFView < PDFView

  DragImage = NSImage.imageNamed 'Generic'

  def hitTest(point)
    NSPointInRect(point, frame) ? self : nil
  end

  def shouldDelayWindowOrderingForEvent(event)
    true
  end

  def acceptsFirstMouse(event)
    true
  end

  def draggingSourceOperationMaskForLocal(flag)
    NSDragOperationCopy
  end

  def menuForEvent(event)
    nil
  end

  def mouseDown(event)
    NSApp.preventWindowOrdering
    @mouseDownEvent = event
  end

  def mouseDragged(event)
    start = @mouseDownEvent.locationInWindow
    current = event.locationInWindow
    
    return if distanceSquaredBetweenPoints(start, current) < 52.0

    dragStart = convertPoint start, fromView:nil
    imageSize = DragImage.size

    dragStart.x -= imageSize.width / 3.0
    dragStart.y -= imageSize.height / 3.0

    page = currentPage
    d = page.dataRepresentation
    dPboard = NSPasteboard.pasteboardWithName NSDragPboard
    dPboard.declareTypes [NSPDFPboardType], owner:self
    dPboard.setData(d, forType:NSPDFPboardType)
    dragImage DragImage, at:dragStart, offset:NSMakeSize(0, 0), 
      event:@mouseDownEvent, pasteboard:dPboard, source:self, slideBack:true
    @mouseDownEvent = nil
  end
  
  def mouseUp(event)
    @mouseDownEvent = nil
  end
end