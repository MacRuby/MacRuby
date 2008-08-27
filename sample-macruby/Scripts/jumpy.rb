# This script is based on Jumpy.app, a CoreAnimation sample written by Lucas Newman, 
# of Delicious Monster Software.

framework 'Cocoa'
framework 'QuartzCore'
framework 'ApplicationServices'

class JumpyController

  ANIMATION_DURATION = 4.0

  def applicationDidFinishLaunching(notification)
    # create a window the size of the screen and set it to solid black
    displayBounds = NSScreen.mainScreen.frame
    @mainWindow = NSWindow.alloc.initWithContentRect displayBounds, styleMask:NSBorderlessWindowMask, backing:NSBackingStoreRetained, defer:false, screen:NSScreen.mainScreen
    contentView = @mainWindow.contentView
    contentView.wantsLayer = true
    #contentView.layer.backgroundColor = CGColorCreateGenericGray(0.0, 1.0)
    
    # create a snapshot of the screen
    image = CGWindowListCreateImage(CGRectInfinite, KCGWindowListOptionOnScreenOnly, KCGNullWindowID, KCGWindowImageDefault)
    
    # create a layer for the current screen
    screenLayer = CALayer.layer
    screenLayer.frame = [0, 0, CGImageGetWidth(image), CGImageGetHeight(image)]
    screenLayer.contents = image
    
    # add a container which will hold our screen and reflection layers
    containerLayer = CALayer.layer
    containerLayer.frame = screenLayer.frame
    containerLayer.addSublayer screenLayer
    
    # create the reflection layer
    reflectionLayer = CALayer.layer
    reflectionLayer.contents = screenLayer.contents # share the contents image with the screen layer
    reflectionLayer.opacity = 0.4
    reflectionLayer.frame = CGRectOffset(screenLayer.frame, 0.5, -NSHeight(displayBounds) + 0.5)
    reflectionLayer.transform = CATransform3DMakeScale(1.0, -1.0, 1.0) # flip the y-axis
    reflectionLayer.sublayerTransform = reflectionLayer.transform
    containerLayer.addSublayer reflectionLayer
    
    # create a shadow layer which lies on top of the reflection layer
    shadowLayer = CALayer.layer
    shadowLayer.frame = reflectionLayer.bounds
    shadowLayer.delegate = self
    shadowLayer.setNeedsDisplay # this invokes -drawLayer:inContext: below, since we are the delegate
    reflectionLayer.addSublayer shadowLayer
    
    # add the container layer to the window's content view's layer
    contentView.layer.addSublayer containerLayer
    
    # place the window on top of everything
    @mainWindow.level = CGShieldingWindowLevel()
    @mainWindow.makeKeyAndOrderFront nil
    
    # animate the screen away
    CATransaction.begin
    CATransaction.setValue ANIMATION_DURATION, forKey:KCATransactionAnimationDuration
    
    # scale it down
    shrinkAnimation = CABasicAnimation.animationWithKeyPath "transform.scale"
    shrinkAnimation.timingFunction = CAMediaTimingFunction.functionWithName KCAMediaTimingFunctionEaseIn
    shrinkAnimation.toValue = 0.0
    containerLayer.addAnimation shrinkAnimation, forKey:"shrinkAnimation"
    
    # fade it out
    fadeAnimation = CABasicAnimation.animationWithKeyPath "opacity"
    fadeAnimation.toValue = 0.0
    fadeAnimation.timingFunction = CAMediaTimingFunction.functionWithName KCAMediaTimingFunctionEaseIn
    containerLayer.addAnimation fadeAnimation, forKey:"fadeAnimation"
    
    # make it jump a couple of times
    positionAnimation = CAKeyframeAnimation.animationWithKeyPath "position"
    
    positionPath = CGPathCreateMutable()
    CGPathMoveToPoint(positionPath, nil, containerLayer.position.x, containerLayer.position.y)
    CGPathAddQuadCurveToPoint(positionPath, nil, containerLayer.position.x, containerLayer.position.y, containerLayer.position.x, containerLayer.position.y)
    CGPathAddQuadCurveToPoint(positionPath, nil, containerLayer.position.x, containerLayer.position.y * 2, containerLayer.position.x, containerLayer.position.y)
    CGPathAddQuadCurveToPoint(positionPath, nil, containerLayer.position.x, containerLayer.position.y * 3, containerLayer.position.x, containerLayer.position.y)
    
    positionAnimation.path = positionPath
    positionAnimation.timingFunction = CAMediaTimingFunction.functionWithName KCAMediaTimingFunctionEaseIn
    containerLayer.addAnimation positionAnimation, forKey:"positionAnimation"
    
    CATransaction.commit
    
    # quit after animating
    NSApp.performSelector :"terminate:", withObject:nil, afterDelay:ANIMATION_DURATION
  end

  def applicationWillTerminate(notification)
    @mainWindow.orderOut self
  end

  def drawLayer(layer, inContext:context)
    NSGraphicsContext.saveGraphicsState
    NSGraphicsContext.currentContext = NSGraphicsContext.graphicsContextWithGraphicsPort context, flipped:false
    
    shadowGradient = NSGradient.alloc.initWithColors [NSColor.clearColor, NSColor.blackColor]
    layerRect = CGContextGetClipBoundingBox(context)
    shadowGradient.drawFromPoint NSPoint.new(0, layerRect.origin.y + layerRect.size.height), toPoint:NSPoint.new(0, layerRect.origin.y), options:NSGradientDrawsAfterEndingLocation
    
    NSGraphicsContext.restoreGraphicsState
  end

end

app = NSApplication.sharedApplication
app.delegate = JumpyController.new
app.run
