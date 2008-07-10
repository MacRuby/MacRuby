HotCocoa::Mappings.map :scroll_view => :NSScrollView do
  
  defaults :vertical_scroller => true, :horizontal_scroller => true, :layout => {}
  
  def init_with_options(scroll_view, options)
    scroll_view.initWithFrame options.delete(:frame)
  end
  
  custom_methods do
    
    def <<(view)
      setDocumentView(view)
    end
    
    def vertical_scroller=(value)
      setHasVerticalScroller(value)
    end
    
    def horizontal_scroller=(value)
      setHasHorizontalScroller(value)
    end
    
  end
  
end
