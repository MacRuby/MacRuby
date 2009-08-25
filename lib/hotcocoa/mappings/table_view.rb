HotCocoa::Mappings.map :table_view => :NSTableView do
  
  defaults :column_resize => :uniform, :frame => DefaultEmptyRect, :layout => {}
  
  constant :column_resize, {
    :none               => NSTableViewNoColumnAutoresizing,
    :uniform            => NSTableViewUniformColumnAutoresizingStyle,
    :sequential         => NSTableViewSequentialColumnAutoresizingStyle,
    :reverse_sequential => NSTableViewReverseSequentialColumnAutoresizingStyle,
    :last_column_only   => NSTableViewLastColumnOnlyAutoresizingStyle,
    :first_column_only  => NSTableViewFirstColumnOnlyAutoresizingStyle
  }
  
  constant :grid_style, { 
    :none               => NSTableViewGridNone, 
    :vertical           => NSTableViewSolidVerticalGridLineMask, 
    :horizontal         => NSTableViewSolidHorizontalGridLineMask, 
  	:both               => NSTableViewSolidVerticalGridLineMask | NSTableViewSolidHorizontalGridLineMask 
  }

  constant :selection_style, {
    :regular            => NSTableViewSelectionHighlightStyleRegular,
    :source_list        => NSTableViewSelectionHighlightStyleSourceList
  }

  def init_with_options(table_view, options)
    table_view.initWithFrame(options.delete(:frame))
  end

  custom_methods do
    
    def data=(data_source)
      data_source = HotCocoa::TableDataSource.new(data_source) if data_source.kind_of?(Array)
      setDataSource(data_source)
    end
    
    def columns=(columns)
      columns.each do |column|
        addTableColumn(column)
      end
    end
    
    def column=(column)
      addTableColumn(column)
    end
    
    def auto_size
      setAutoresizingMask(NSViewHeightSizable|NSViewWidthSizable)
    end
    
    def column_resize=(style)
      setColumnAutoresizingStyle(style)
    end

    def reload 
   	  reloadData 
   	end 
   	
   	def grid_style=(value) 
   	  setGridStyleMask(value) 
   	end

    def on_double_action=(behavior)
      if target && (
        target.instance_variable_get("@action_behavior") || 
        target.instance_variable_get("@double_action_behavior"))
          object.instance_variable_set("@double_action_behavior", behavior)
          object = target
      else
        object = Object.new
        object.instance_variable_set("@double_action_behavior", behavior)
        setTarget(object)
      end
      def object.perform_double_action(sender)
        @double_action_behavior.call(sender)
      end
      setDoubleAction("perform_double_action:")
    end
   
    def on_double_action(&behavior)
      self.on_double_action = behavior
      self
    end

  end

  delegating "tableView:willDisplayCell:forTableColumn:row:",               :to => :will_display_cell,      :parameters => [:willDisplayCell, :forTableColumn, :row]
  delegating "tableView:dataCellForTableColumn:row:",                       :to => :data_cell,              :parameters => [:dataCellForTableColumn, :row]
  delegating "tableView:shouldShowCellExpansionForTableColumn:row:",        :to => :expand_cell?,           :parameters => [:shouldShowCellExpansionForTableColumn, :row]
  delegating "tableView:isGroupRow:",                                       :to => :is_group_row?,          :parameters => [:isGroupRow]
  delegating "tableView:shouldEditTableColumn:row:",                        :to => :edit_table_column?,     :parameters => [:shouldEditTableColumn, :row]
  delegating "tableView:heightOfRow:",                                      :to => :height_of_row,          :parameters => [:heightOfRow]
  delegating "selectionShouldChangeInTableView:",                           :to => :change_selection?
  delegating "tableView:shouldSelectRow:",                                  :to => :select_row?,            :parameters => [:shouldSelectRow]
  delegating "tableView:selectionIndexesForProposedSelection:",             :to => :indexes_for_selection,  :parameters => [:selectIndexesForProposedSelection]
  delegating "tableView:shouldSelectTableColumn:",                          :to => :select_column?,         :parameters => [:shouldSelectTableColumn]
  delegating "tableViewSelectionIsChanging:",                               :to => :selection_changing,     :parameters => [:tableViewSelectionIsChanging]
  delegating "tableViewSelectionDidChange:",                                :to => :selection_changed,      :parameters => [:tableViewSelectionDidChange]
  delegating "tableView:shouldTypeSelectForEvent:withCurrentSearchString:", :to => :type_select_for_event?, :parameters => [:shouldTypeSelectForEvent, :withCurrentSearchString]
  delegating "tableView:typeSelectStringForTableColumn:row:",               :to => :type_select_string,     :parameters => [:typeSelectStringForTableColumn, :row]
  delegating "tableView:nextTypeSelectMatchFromRow:toRow:forString:",       :to => :find_in_range,          :parameters => [:nextTypeSelectMatchFromRow, :toRow, :forString]
  delegating "tableView:didDragTableColumn:",                               :to => :dragged_column,         :parameters => [:didDragTableColumn]
  delegating "tableViewColumnDidMove:",                                     :to => :column_moved,           :parameters => [:tableViewColumnDidMove]
  delegating "tableViewColumnDidResize:",                                   :to => :column_resized,         :parameters => [:tableViewColumnDidResize]
  delegating "tableView:didClickTableColumn:",                              :to => :clicked_column,         :parameters => [:didClickTableColumn]
  delegating "tableView:mouseDownInHeaderOfTableColumn:",                   :to => :header_clicked,         :parameters => [:mouseDownInHeaderOfTableColumn]
  delegating "tableView:shouldTrackCell:forTableColumn:row:",               :to => :track_cell?,            :parameters => [:shouldTrackCell, :forTableColumn, :row]
  delegating "tableView:toolTipForCell:rect:tableColumn:row:mouseLocation:",:to => :tooltip_for_cell,       :parameters => [:toolTipForCell, :rect, :tableColumn, :row, :mouseLocation]

end