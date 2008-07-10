HotCocoa::Mappings.map :popup => :NSPopUpButton do
  
  defaults :pulls_down => false, 
           :frame => DefaultEmptyRect,
           :layout => {}

  def init_with_options(popup, options)
    popup.initWithFrame(options.delete(:frame), pullsDown:options.delete(:pulls_down))
  end
  
  custom_methods do
    
    class ItemList
      
      include Enumerable
      
      attr_reader :control
      
      def initialize(control)
        @control = control
      end
      
      def <<(title)
        control.addItemWithTitle(title)
      end
      
      def [](index)
        control.itemTitleAtIndex(index)
      end
      
      def delete(title)
        if title.kind_of?(Fixnum)
          control.removeItemAtIndex(title)
        else
          control.removeItemWithTitle(title)
        end
      end
      
      def insert(title, at:index)
        control.insertItemWithTitle(title, atIndex:index)
      end
      
      def selected
        control.titleOfSelectedItem
      end
      
      def selected=(title)
        if title.kind_of?(Fixnum)
          control.selectItemAtIndex(title)
        else
          control.selectItemWithTitle(title)
        end
      end
      
      def selected_index
        control.indexOfSelectedItem
      end
      
      def size
        control.numberOfItems
      end
      
      def each(&block)
        control.itemTitles.each(&block)
      end
      
    end
    
    class MenuItemList < ItemList
      include Enumerable
      
      def selected
        control.selectedItem
      end

      def selected=(menu_item)
        if menu_item.kind_of?(Fixnum)
          control.selectItemAtIndex(menu_item)
        else
          control.selectItem(menu_item)
        end
      end

      def [](index)
        control.itemAtIndex(index)
      end
      
      def each(&block)
        control.itemArray.each(&block)
      end
    end
    
    def items=(values)
      removeAllItems
      addItemsWithTitles(values)
    end
    
    def items
      @_item_list ||= ItemList.new(self)
    end
    
    def menu_items
      @_menu_item_list ||=  MenuItemList.new(self)
    end
    
  end
  
end