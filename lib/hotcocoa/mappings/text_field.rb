HotCocoa::Mappings.map :text_field => :NSTextField do
  
  defaults :selectable => true, :editable => true, :layout => {}
  
  def init_with_options(text_field, options)
    text_field.initWithFrame options.delete(:frame)
  end
  
end
