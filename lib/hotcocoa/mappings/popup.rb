HotCocoa::Mappings.map :popup => :NSPopUpButton do
  
  defaults :pulls_down => false, 
           :frame => DefaultEmptyRect

  def init_with_options(popup, options)
    popup.initWithFrame(options.delete(:frame), pullsDown:options.delete(:pulls_down))
  end
  
  custom_methods do
    
    class ItemList
      
      attr_reader :control
      
      def initialize(control)
        @control = control
      end
      
      def <<(title)
        control.addItemWithTitle(title)
      end
      
      def delete(title)
        control.removeItemWithTitle(title)
      end
      
      def insert(title, at:index)
        control.insertItemWithTitle(title, atIndex:index)
      end
      
      def selected
        control.titleOfSelectedItem
      end
      
      def selected_index
        control.indexOfSelectedItem
      end
      
      def size
        control.numberOfItems
      end
    end
    
    def items=(values)
      removeAllItems
      addItemsWithTitles(values)
    end
    
    def items
      @_item_list ||= ItemList.new(self)
    end
    
  end
  
end