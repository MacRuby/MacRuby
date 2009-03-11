module HotCocoa
  class TableDataSource
    attr_reader :data

    def initialize(data)
      @data = data
    end

    def numberOfRowsInTableView(tableView)
      data.length
    end

    def tableView(view, objectValueForTableColumn:column, row:i)
      data[i][column.identifier.intern]
    end

  end
end