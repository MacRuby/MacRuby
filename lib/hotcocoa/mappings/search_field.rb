HotCocoa::Mappings.map :search_field => :NSSearchField do

  defaults :layout => {}, :frame => DefaultEmptyRect
  
  def init_with_options(search_field, options)
    search_field.initWithFrame options.delete(:frame)
  end

end