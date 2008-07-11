HotCocoa::Mappings.map :split_view => :NSSplitView do
  
  defaults :layout => {}
  
  def init_with_options(split_view, options)
    split_view.initWithFrame options.delete(:frame)
  end
  
  custom_methods do
    
    def horizontal=(value)
      setVertical(!value)
    end
    
    def set_position(position, of_divider_at_index:index)
      setPosition position, ofDividerAtIndex:index
    end
    
  end
  
end
