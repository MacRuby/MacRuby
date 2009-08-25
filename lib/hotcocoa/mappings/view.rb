
HotCocoa::Mappings.map :view => :NSView do

  defaults :frame => DefaultEmptyRect,
           :layout => {}

  constant :auto_resize, {
    :none   => NSViewNotSizable,
    :width  => NSViewWidthSizable,
    :height => NSViewHeightSizable,
    :min_x  => NSViewMinXMargin,
    :min_y  => NSViewMinYMargin,
    :max_x  => NSViewMaxXMargin,
    :max_y  => NSViewMaxYMargin
  }
  
  constant :border, {
    :none           => NSNoBorder,
    :line           => NSLineBorder,
    :bezel          => NSBezelBorder,
    :groove         => NSGrooveBorder
  }

  def init_with_options(view, options)
    view.initWithFrame options.delete(:frame)
  end
  
  custom_methods do

    def auto_resize=(value)
      setAutoresizingMask(value)
    end
    
    def <<(view)
      addSubview(view)
    end
    
    def layout=(options)
      @layout = HotCocoa::LayoutOptions.new(self, options)
      @layout.update_layout_views!
    end
    
    def layout
      @layout
    end

    def on_notification(options={}, &block)
      options[:sent_by] = self
      NotificationListener.new(options, &block)
    end

    def remove(subview, options = {})
      raise ArgumentError, "#{subview} is not a subview of #{self} and cannot be removed." unless subview.superview == self
      options[:needs_display] == false ? subview.removeFromSuperviewWithoutNeedingDisplay : subview.removeFromSuperview
    end
    
    def enter_full_screen(options={})
      enterFullScreenMode((options.delete(:screen) || NSScreen.mainScreen), withOptions:options)
    end
    
    def leave_full_screen(options={})
      exitFullScreenModeWithOptions(options)
    end
    
  end
    
end