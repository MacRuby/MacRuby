class RoundCloseButtonCell < NSButtonCell

  def drawWithFrame(cellFrame, inView:controlView)
    if NSGraphicsContext.currentContextDrawingToScreen
      if isHighlighted
        NSColor.orangeColor.set
      else
        NSColor.blueColor.set
      end
      NSBezierPath.fillRect cellFrame
      NSBezierPath.defaultLineWidth = 3
      NSColor.whiteColor.set
      NSBezierPath.strokeRect cellFrame
      p = NSBezierPath.new
      p.lineWidth = 3.0
      p.lineCapStyle = NSSquareLineCapStyle
      
      xRect = NSInsetRect(cellFrame, 9,7)
      p.moveToPoint xRect.origin
      p.lineToPoint NSMakePoint(xRect.origin.x + xRect.size.width, xRect.origin.y + xRect.size.height)
      
      p.moveToPoint NSMakePoint(xRect.origin.x, xRect.origin.y + xRect.size.height)
      p.lineToPoint NSMakePoint(xRect.origin.x + xRect.size.width, xRect.origin.y)
      
      NSColor.whiteColor.set
      p.stroke
    end
  end
  
end