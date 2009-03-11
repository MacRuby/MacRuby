HotCocoa::Mappings.map :text_view => :NSTextView do

  defaults :layout => {}, :frame => DefaultEmptyRect
  
  def init_with_options(text_view, options)
    if options[:container]
      text_view.initWithFrame options.delete(:frame), :textContainer => options.delete(:container)
    else
      text_view.initWithFrame options.delete(:frame)
    end
  end
  
end
