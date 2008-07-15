HotCocoa::Mappings.map :scroll_view => :NSScrollView do
  
  defaults :vertical_scroller => true, :horizontal_scroller => true, :autoresizes_subviews => true, :layout => {}, :frame => [0,0,0,0] 
  
  def init_with_options(scroll_view, options)
    scroll_view.initWithFrame options.delete(:frame)
  end
  
  custom_methods do
    
    def <<(view)
      setDocumentView(view)
    end
    
    def document_view=(view)
      setDocumentView(view)
    end
    
    def background=(value)
      setDrawsBackground(value)
    end
    
    def vertical_scroller=(value)
      setHasVerticalScroller(value)
    end
    
    def horizontal_scroller=(value)
      setHasHorizontalScroller(value)
    end
    
    def autoresizes_subviews=(value)
      setAutoresizesSubviews(value)
    end
    
  end
  
end
