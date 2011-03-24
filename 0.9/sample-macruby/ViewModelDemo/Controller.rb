class Controller

  attr_accessor :mySuperview, :numberText, :speedText, :numberSlider, :speedSlider
	
  def init
    super

    @objs = [] # This will hold the ViewModel objects.
		
    # A global flag to keep track of whether one of the subviews is moving.
    # There is no particular reason why I used a global instead of a method call
    # to mySuperview to retrieve an objMoving instance variable. A global
    # just seems perfectly acceptable for this kind of thing.
    $objMoving = false

    self
  end
	
  def awakeFromNib
    for n in 0..(@numberSlider.intValue-1)
      addNewViewModel
    end
  end
	
  def adjustNumberOfViewModels
    desired = @numberSlider.intValue
    current = @objs.length
		
    if current < desired
      for n in 1..(desired-current)
        addNewViewModel
      end
    end
		
    if current > desired
      until @objs.length == desired
        @objs.last.removeFromSuperview
        @objs.pop
      end
    end
  end
	
  def addNewViewModel
    myRect = getRandomViewRect
    # Create a new ViewModel object, which is a subclass of NSView.
    myObj = ViewModel.alloc.initWithFrame myRect
    # Just for testing, the object knows its number.
    myObj.setNum @objs.size
    # Add it to the superview
    @mySuperview.addSubview myObj
    # Add it to the array
    @objs.push myObj
  end
	
  def numberSliderAction(sender)
    number = @numberSlider.intValue
    @numberText.stringValue = "Number: " + number.to_s
    adjustNumberOfViewModels
  end
  
  def moveButtonAction(sender)
    # Assign new destinations to the objects
    @objs.each do |obj|
      destPt = getRandomViewPoint
      obj.setMoveDestination destPt
      speed = @speedSlider.floatValue
      obj.setSpeed speed
      obj.startMovement
    end
  end
  	
  def speedSliderAction(sender)
    # No need to store the speed in an instance variable, since the speed is read
    # right from the control in moveButtonAction. Just update the speed text.
    speed = @speedSlider.floatValue
    @objs.each do |obj|
      if obj.speed > 0 # object is moving
        obj.setSpeed speed
      end
    end
    speed = speed.to_i
    @speedText.stringValue = "Speed: " + speed.to_s
  end

  def getRandomViewRect
    rx = rand(@mySuperview.bounds.size.width - SQUARE_SIZE)
    ry = rand(@mySuperview.bounds.size.height - SQUARE_SIZE)
    NSRect.new(NSPoint.new(rx, ry), NSSize.new(SQUARE_SIZE, SQUARE_SIZE))
  end

  def getRandomViewPoint
    rx = rand(@mySuperview.bounds.size.width - SQUARE_SIZE)
    ry = rand(@mySuperview.bounds.size.height - SQUARE_SIZE)
    NSPoint.new(rx, ry)
  end
	
  def handleEvents
    app = NSApplication.sharedApplication
    event = app.nextEventMatchingMask NSAnyEventMask,
      untilDate:NSDate.dateWithTimeIntervalSinceNow(0.01),
      inMode:OSX.NSDefaultRunLoopMode,
      dequeue:true
    if event
      # Could put special event handling here.
      app.sendEvent(event)
    end
  end
	
end
