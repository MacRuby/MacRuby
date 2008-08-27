HotCocoa::Mappings.map :text_field => :NSTextField do

  constant :text_align, {
    :right  => NSRightTextAlignment,
    :left   => NSLeftTextAlignment,
    :center => NSCenterTextAlignment
  }
  
  defaults :selectable => true, :editable => true, :layout => {}, :frame => DefaultEmptyRect
  
  def init_with_options(text_field, options)
    text_field.initWithFrame options.delete(:frame)
  end
  
  custom_methods do

    def text_align=(value)
      setAlignment(value)
    end
    
    def text_color=(value)
      setTextColor(value)
    end

  end

end
