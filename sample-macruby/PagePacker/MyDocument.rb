class MyDocument < NSDocument
  attr_accessor :packerView
  
  def init
    super
    @packModel = PackModel.new
    @packModel.setUndoManager undoManager
    self
  end
  
  def windowNibName
    @nib_name ||= 'MyDocument'
  end

  def updateUI
    @packerView.setPackModel @packModel
    @packerView.window.nextResponder = CatalogController.sharedCatalogController
  end

  def windowControllerDidLoadNib(controller)
    super
    updateUI
  end

  def dataOfType(type, error:outError)
    NSKeyedArchiver.archivedDataWithRootObject @packModel
  end

  def readFromData(data, ofType:type, error:outError)
    @packModel = NSKeyedUnarchiver.unarchiveObjectWithData data
    @packModel.setUndoManager undoManager
    updateUI if @packerView
    true
  end

  def printOperationWithSettings(printSettings, error:outError)
    NSPrintOperation.printOperationWithView @packerView, printInfo:printInfo
  end
end