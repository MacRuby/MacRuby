# Description:  This is the implementation file for the Controller class, which implements the #changeTransparency
#               action, called when the slider on the window is moved.

class Controller < NSObject
  # Kinda the same as IBOutlet NSWindow *itsWindow; on Objective-C
  attr_writer :itsWindow

  # This method changes the transparency for the *entire window*, not some particular object.  Thus,
  # all objects drawn in this window, even if drawn at full alpha value, will pick up this setting.
  def changeTransparency(sender)
    # set the window's alpha value from 0.0-1.0
    @itsWindow.setAlphaValue(sender.floatValue)
    # go ahead and tell the window to redraw things, which has the effect of calling CustomView's -drawRect: routine
    @itsWindow.display
  end

end