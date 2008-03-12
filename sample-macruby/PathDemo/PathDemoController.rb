class PathDemoController

  ib_outlet :button, :popup, :window, :demoView

  def awakeFromNib
    @popup.removeAllItems
    ['Rectangles', 'Circles', 'Bezier Paths', 'Circle Clipping'].each do |title|
      @popup.addItemWithTitle title
    end
  end

  def runAgain(sender)
    select(self)
  end
  ib_action :runAgain
  
  def select(sender)
    @demoView.demoNumber = @popup.indexOfSelectedItem
    @demoView.needsDisplay = true
  end
  ib_action :select

  def print(sender)
    info = NSPrintInfo.sharedPrintInfo
	printOp = NSPrintOperation.printOperationWithView @demoView, printInfo:info
    printOp.showPanels = true
    printOp.runOperation
  end
  ib_action :print

  def applicationShouldTerminateAfterLastWindowClosed(application)
    true
  end

end
