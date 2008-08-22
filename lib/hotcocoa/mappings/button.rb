HotCocoa::Mappings.map :button => :NSButton do
  
  defaults :bezel => :rounded, 
           :frame => DefaultEmptyRect,
           :layout => {}
  
  constant :bezel, {
     :rounded             => NSRoundedBezelStyle,
     :regular_square      => NSRegularSquareBezelStyle,
     :thick_square        => NSThickSquareBezelStyle,
     :thicker_square      => NSThickerSquareBezelStyle,
     :disclosure          => NSDisclosureBezelStyle,
     :shadowless_square   => NSShadowlessSquareBezelStyle,
     :circular            => NSCircularBezelStyle,
     :textured_square     => NSTexturedSquareBezelStyle,
     :help_button         => NSHelpButtonBezelStyle,
     :small_square        => NSSmallSquareBezelStyle,
     :textured_rounded    => NSTexturedRoundedBezelStyle,
     :round_rect          => NSRoundRectBezelStyle,
     :recessed            => NSRecessedBezelStyle,
     :rounded_disclosure  => NSRoundedDisclosureBezelStyle
  }
  
  constant :type, {
     :momentary_light     => NSMomentaryLightButton,
     :push_on_push_off    => NSPushOnPushOffButton,
     :toggle              => NSToggleButton,
     :switch              => NSSwitchButton,
     :radio               => NSRadioButton,
     :momentary_change    => NSMomentaryChangeButton,
     :on_off              => NSOnOffButton,
     :momentary_push_in   => NSMomentaryPushInButton,
     :momentary_push      => NSMomentaryPushButton,
     :momentary_light     => NSMomentaryLight
  }

  constant :state, {
    :on                   => NSOnState,
    :off                  => NSOffState,
    :mixed                => NSMixedState
  }
  
  def init_with_options(button, options)
    button.initWithFrame options.delete(:frame)
  end

  custom_methods do
    
    def bezel=(value)
      setBezelStyle(value)
    end
    
    def type=(value)
      setButtonType(value)
    end

    def state=(value)
      case value 
        when :on
          value = NSOnState
        when :off
          value = NSOffState
        when :mixed
          value = NSMixedState
      end 
      setState(value)
    end
    
    def on?
      state == NSOnState
    end

    def off?
      state == NSOffState
    end

    def mixed?
      state == NSMixedState
    end
    
  end
  
end
