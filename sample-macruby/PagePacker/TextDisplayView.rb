class TextDisplayView < NSView
  def initWithPageSize(size, attributedString:attString)
    frame = NSRect.new(NSZeroPoint, size)
    @pageSize = size
    @attString = attString.copy
    initWithFrame frame
  end

  def isFlipped; true; end

  def drawRect(rect)
    rect = NSInsetRect(self.bounds, 3, 3)
    @attString.drawInRect rect
  end
end