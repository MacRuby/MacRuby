HotCocoa::Mappings.map :box => :NSBox do
  
  defaults :frame => DefaultEmptyRect, :layout => {}
  
  constant :title_position, {
    :none           => NSNoTitle,
    :above_top      => NSAboveTop,
    :top            => NSAtTop,
    :below_top      => NSBelowTop,
    :above_bottom   => NSAboveBottom,
    :bottom         => NSAtBottom,
    :below_bottom   => NSBelowBottom
  }
  
  constant :type, {
    :primary        => NSBoxPrimary,
    :secondary      => NSBoxSecondary,
    :separator      => NSBoxSeparator,
    :old            => NSBoxOldStyle,
    :custom         => NSBoxCustom
  }
  
  def init_with_options(box, options)
    box.initWithFrame options.delete(:frame)
  end
  
  custom_methods do
    
    def type=(value)
      setBoxType(value)
    end

    def border=(value)
      setBorderType(value)
    end
    
  end
  
end
