HotCocoa::Mappings.map :label => :NSTextField do
  
  defaults :selectable => false, :bordered => false, :drawsBackground => false, :frame => DefaultEmptyRect, :layout => {}
  
  def init_with_options(text_field, options)
    tf = text_field.initWithFrame options.delete(:frame)
    tf.editable = false
    tf
  end
  
end
