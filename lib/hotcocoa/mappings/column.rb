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

  end

end