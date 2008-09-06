class SimpleLayoutView < NSView
  
  attr_accessor :boxColorField
  
  # Default separation between items, and default size of added subviews
  SEPARATION = 10.0
  BOXWIDTH = 80.0
  BOXHEIGHT = 80.0
  
  # Layout styles
  ColumnLayout = 0
  RowLayout = 1
  GridLayout = 2
  
  # By default NSColorPanel does not show an alpha (opacity) slider; enable it
  def awakeFromNib
    NSColorPanel.sharedColorPanel.showsAlpha = true
  end

  # Start off in column mode
  attr_reader :layoutStyle
  def initWithFrame frame
    if super
      @layoutStyle = ColumnLayout
    end
    return self
  end

  def setLayoutStyle style
    if @layoutStyle != style
      @layoutStyle = style
      layout!
    end
  end
  
  # This method returns a rect that is integral in base coordinates
  def integralRect rect
    convertRectFromBase NSIntegralRect(convertRectToBase(rect))
  end

  # This method simply computes the new layout, and calls setFrame: on all 
  # subview with their locations. Since the calls are made to the subviews' 
  # animators, the subview animate to their new locations.
  def layout!
    case @layoutStyle
    when ColumnLayout
      # Starting point: center bottom of view.
      point = NSPoint.new(bounds.size.width / 2.0, 0.0)
      subviews.each do |view|
        # Centered horizontally, stacked higher.
        frame = NSRect.new(NSPoint.new(point.x - BOXWIDTH / 2.0, point.y),
                           NSSize.new(BOXWIDTH, BOXHEIGHT))
        view.animator.frame = integralRect(frame)
        # Next view location; we're stacking higher.
        point.y += frame.size.width + SEPARATION
      end
    when RowLayout
      # Starting point: center left edge of view.
      point = NSPoint.new(0.0, bounds.size.height / 2.0)
      subviews.each do |view|
        # Centered vertically, stacked left to right.
        frame = NSRect.new(NSPoint.new(point.x, point.y - BOXHEIGHT / 2.0),
                           NSSize.new(BOXWIDTH, BOXHEIGHT))
        view.animator.frame = integralRect(frame)
        # Next view location.
        point.x += frame.size.width + SEPARATION
      end
    when GridLayout
      # Put the views in a roughly square grid.
      viewsPerSide = Math.sqrt(subviews.size).ceil
      i = 0
      # Starting at the bottom left corner.
      point = NSZeroPoint.dup
      subviews.each do |view|
        frame = NSRect.new(NSPoint.new(point.x, point.y),
                           NSSize.new(BOXWIDTH, BOXHEIGHT))
        view.animator.frame = integralRect(frame)
        # Stack them horizontally.
        point.x += BOXWIDTH + SEPARATION
        # And if we have enough on this row, move up to the next.
        if (i += 1) % viewsPerSide == 0
          point.x = 0
          point.y += BOXHEIGHT + SEPARATION
        end
      end
    end
  end

  # Changing frame (which is what happens when the window is resized) should 
  # cause relayout.
  def setFrameSize frame
    super
    layout!
  end

  # Create a new view to be added/animated. Any kind of view can be added here, 
  # we go for simple colored box using the Leopard "custom" box type.
  def viewToBeAdded
    frame = NSRect.new(NSZeroPoint, NSSize.new(BOXWIDTH, BOXHEIGHT))
    box = NSBox.alloc.initWithFrame frame
    box.boxType = NSBoxCustom
    box.borderType = NSLineBorder
    box.titlePosition = NSNoTitle
    box.fillColor = @boxColorField.color
    return box
  end
  
  # Action methods to add/remove boxes, giving us something to animate.  
  # Note that we cause a relayout here; a better design is to relayout in the 
  # view automatically on addition/removal of subviews.
  def addABox(sender)
    addSubview viewToBeAdded
    layout!
  end
  
  def removeLastBox(sender)
    subviews.lastObject.removeFromSuperview
    layout!
  end
  
  # Action method to change layout style.
  def changeLayout(sender)
    setLayoutStyle sender.selectedTag
  end

end