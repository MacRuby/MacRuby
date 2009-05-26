HotCocoa::Mappings.map :column => :NSTableColumn do

  defaults :title => 'Column'

  def init_with_options(column, options)
    column.initWithIdentifier(options.delete(:id))
  end

  custom_methods do
    
    def title
      headerCell.stringValue
    end

    def title=(newTitle)
      headerCell.stringValue = newTitle
    end

    def data_cell=(cell) 
      setDataCell(cell) 
    end 
  
    def max_width=(val) 
      setMaxWidth(val) 
    end 
   
    def min_width=(val) 
      setMinWidth(val) 
    end
  
  end

end