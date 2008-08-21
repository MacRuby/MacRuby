class DataSource

  def outlineView outlineView, numberOfChildrenOfItem:item
    item ? item.numberOfChildren : 1
  end
  
  def outlineView outlineView, isItemExpandable:item
    item ? item.numberOfChildren != -1 : true
  end
  
  def outlineView outlineView, child:index, ofItem:item
    item ? item.childAtIndex(index) : FileSystemItem.rootItem
  end
  
  def outlineView outlineView, objectValueForTableColumn:tableColumn, byItem:item
    if item
      s = item.relativePath
    else
      '/'
    end
  end
  
  def outlineView outlineView, shouldEditTableColumn:tableColumn, item:item
    false
  end
  
end
