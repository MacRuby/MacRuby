require 'hotcocoa'
SOURCE_DIR = File.expand_path(File.dirname(__FILE__))
require SOURCE_DIR + '/custom_view_behaviors'
require SOURCE_DIR + '/custom_window_behaviors'

class Application
  include HotCocoa
  
  def start
    application :name => "HotCocoa: Round Transparent Window" do |app|
      app.delegate = self
      @main_window = window :frame => [434, 297, 250, 250],
                            :title => "The NSBorderlessWindowMask style makes this title bar go away",
                            :style => [:borderless] do |win|
        win.setBackgroundColor(color(:name => 'clear'))
        # This next line pulls the window up to the front on top of other system windows.  This is how the Clock app behaves;
        # generally you wouldn't do this for windows unless you really wanted them to float above everything.
        win.level = NSStatusWindowLevel
        # Let's start with no transparency for all drawing into the window
        win.alphaValue = 1.0
        # but let's turn off opaqueness so that we can see through the parts of the window that we're not drawing into
        win.opaque = false
        # and while we're at it, make sure the window has a shadow, which will automatically be the shape of our custom content.
        win.hasShadow = true
        # We need to extend the window to add support for mouse dragging
        win.extend(CustomWindowBehaviors)
        # and we need to extend the window's content view to override the drawRect method
        win.contentView.extend(CustomViewBehaviors)
        win << slider_layout("Move slider to change transparency") do |slider|
            # set the window's alpha value from 0.0-1.0
            win.alphaValue = slider.floatValue
            # go ahead and tell the window to redraw things, which has the effect of calling win.contentView's -drawRect: routine
            win.display
        end
      end
    end
  end
  
  def slider_layout(label, &block)
    layout_view(:frame => [0, 0, 0, 90], :layout => {:expand => [:height, :width], :start => false}) do |view|
      view << label(:text => label, :layout => {:align => :center})
      s = slider :min => 0, :max => 1.0, :on_action => block, :layout => {:expand => :width, :align => :top}
      s.floatValue = 1.0
      s.frameSize  = [0, 24]
      view << s
    end
  end
  
  # file/open
  def on_open(menu)
  end
  
  # file/new 
  def on_new(menu)
  end
  
  # help menu item
  def on_help(menu)
  end
  
  # This is commented out, so the minimize menu item is disabled
  #def on_minimize(menu)
  #end
  
  # window/zoom
  def on_zoom(menu)
  end
  
  # window/bring_all_to_front
  def on_bring_all_to_front(menu)
  end
  
end

Application.new.start
