HotCocoa::Mappings.map :column => :NSTableColumn do

  def init_with_options(column, options)
    column.initWithIdentifier(options.delete(:id))
  end

  custom_methods do
    
    def text=(value)
      headerCell.setStringValue(value)
    end
    
  end

end