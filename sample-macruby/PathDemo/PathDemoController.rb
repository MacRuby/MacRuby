class PathDemoController

  attr_accessor :button, :popup, :window, :demoView

  def awakeFromNib
    @popup.removeAllItems
    ['Rectangles', 'Circles', 'Bezier Paths', 'Circle Clipping'].each do |title|
      @popup.addItemWithTitle title
    end
  end

  def runAgain(sender)
    select(self)
  end
  
  def select(sender)
    @demoView.demoNumber = @popup.indexOfSelectedItem
    @demoView.needsDisplay = true
  end

  def print(sender)
    info = NSPrintInfo.sharedPrintInfo
	printOp = NSPrintOperation.printOperationWithView @demoView, printInfo:info
    printOp.showPanels = true
    printOp.runOperation
  end

  def applicationShouldTerminateAfterLastWindowClosed(application)
    true
  end

end
