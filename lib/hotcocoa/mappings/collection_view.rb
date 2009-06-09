HotCocoa::Mappings.map :collection_view => :NSCollectionView do
  
  defaults :layout => {}, :frame => [0,0,100,100]
  
  def init_with_options(collection_view, options)
    collection_view.initWithFrame options.delete(:frame)
  end
  
  custom_methods do
    
    def item_prototype
      itemPrototype
    end
    
    def item_view
      item_prototype ? item_prototype.view : nil
    end
    
    def item_view=(view)
      item = NSCollectionViewItem.alloc.init
      item.setView(view)
      view.collection_item = item if view.respond_to?(:collection_item=)
      setItemPrototype(item)
    end
    
    def rows=(value)
      setMaxNumberOfRows(value)
    end
    
    def rows
      maxNumberOfRows
    end
    
    def columns=(value)
      setMaxNumberOfColumns(value)
    end
    
    def columns
      maxNumberOfColumns
    end
    
  end
  
end
