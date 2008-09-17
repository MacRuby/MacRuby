HotCocoa::Mappings.map :text_view => :NSTextView do

  defaults :frame => [0,0,0,0]

  def init_with_options(text_view, options)
    text_view.initWithFrame options.delete(:frame)
  end

end
