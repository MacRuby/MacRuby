# Description:  This class loads the nib and implements the #changeTransparency
#               action, called when the slider on the window is moved.

class NibController < NSWindowController
  attr_writer :itsWindow
  
  def init
    # let's load the nib from our resources folder
    initWithWindowNibName('MainMenu')
    # let's load the window
    window
    NSLog('NibController initialized')
    self
  end

  def windowDidLoad
    NSLog('window loaded')
  end
  
  # This method changes the transparency for the *entire window*, not some particular object.  Thus,
  # all objects drawn in this window, even if drawn at full alpha value, will pick up this setting.
  def changeTransparency(sender)
    # set the window's alpha value from 0.0-1.0
    @itsWindow.setAlphaValue(sender.floatValue)
    # go ahead and tell the window to redraw things, which has the effect of calling CustomView's -drawRect: routine
    @itsWindow.display
  end
  
  def hideOtherApplications(sender)
  end
  
  def unhideAllApplications(sender)
  end
  
  def orderFrontStandardAboutPanel(sender)
  end
  
  def hide(sender)
  end
  
end