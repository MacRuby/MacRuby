HotCocoa::Mappings.map :label => :NSTextField do

  constant :text_align, {
    :right  => NSRightTextAlignment,
    :left   => NSLeftTextAlignment,
    :center => NSCenterTextAlignment
  }
  
  defaults :selectable => false, :bordered => false, :drawsBackground => false, :frame => DefaultEmptyRect, :layout => {}
  
  def init_with_options(text_field, options)
    tf = text_field.initWithFrame options.delete(:frame)
    tf.editable = false
    tf
  end
  
  custom_methods do

    def text_align=(value)
      setAlignment(value)
    end
    
  end
  
end
