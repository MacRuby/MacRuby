# Cocoa class: NSProgressIndicator
# ================================
# 
# Apple Documentation: http://developer.apple.com/DOCUMENTATION/Cocoa/Reference/ApplicationKit/Classes/NSProgressIndicator_Class/Reference/Reference.html
#
# Usage Example:
# --------------
#


HotCocoa::Mappings.map :progress_indicator => :NSProgressIndicator do

  defaults :layout => {}, :frame => [0,0,250,20]
  
  def init_with_options(progress_bar, options)
    progress_bar.initWithFrame(options.delete(:frame))
  end
  
  custom_methods do
    
    def to_f
      doubleValue
    end
    alias :value :to_f
    
    def value=(value)
      setDoubleValue(value.to_f)
    end
    
    def start
      startAnimation(nil)
    end
    
    def stop
      stopAnimation(nil)
    end
    
    def show
      setHidden(false)
    end
    
    def hide
      setHidden(true)
    end
    
    def reset
      setDoubleValue(0.0)
    end
    
    def style=(style_name)
      if style_name == :spinning
        setStyle(NSProgressIndicatorSpinningStyle)
      else
        setStyle(NSProgressIndicatorBarStyle)
      end
    end
    
    def spinning_style
      setStyle(NSProgressIndicatorSpinningStyle)
    end
    
    def bar_style
      setStyle(NSProgressIndicatorBarStyle)
    end
    
  end
  
end