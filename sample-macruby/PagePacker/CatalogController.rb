class CatalogController < NSWindowController
  attr_accessor :pdfView, :pageSlider, :pageField
  
  attr_reader :currentPageIndex
  
  def init
    initWithWindowNibName 'CatalogController'
    @currentPageIndex = 0
    self
  end
  
  def self.sharedCatalogController
    @instance ||= alloc.init
  end

  def windowDidLoad
    path = NSBundle.mainBundle.pathForResource 'pages', ofType:'pdf'
    NSLog('No path for pdf') if path.nil?
    
    url = NSURL.fileURLWithPath path
    pdfDoc = PDFDocument.alloc.initWithURL url
    pageCount = pdfDoc.pageCount
    @pageSlider.numberOfTickMarks = pageCount - 2
    @pageSlider.minValue = 0
    @pageSlider.maxValue = pageCount - 1
    newBounds = NSMakeRect(65,90, 260, 380)
    @pdfView.bounds = newBounds
    @pdfView.document = pdfDoc
    @pdfView.displayMode = KPDFDisplaySinglePage
    @pdfView.window.becomesKeyOnlyIfNeeded = true
    @pdfView.window.nextResponder = self
  end

  def changeToPage(i)
    if @currentPageIndex != i
      doc = @pdfView.document
      if i >= 0 and i < doc.pageCount
        @currentPageIndex = i
        page = doc.pageAtIndex @currentPageIndex
        @pdfView.goToPage page
        @pageField.intValue = @currentPageIndex + 1
      end
    end
  end

  def changePage(sender)
    changeToPage @pageSlider.intValue
  end

  def scrollWheel(event)
    deltaY = event.deltaY
    if deltaY > 0.1
      moveRight nil
    elsif deltaY < -0.1
      moveLeft nil
    end
  end

  def moveLeft(sender)
    changeToPage @currentPageIndex - 1
    @pageSlider.intValue = @currentPageIndex
  end

  def moveDown(sender)
    moveLeft(sender)
  end

  def moveRight(sender)
    changeToPage @currentPageIndex + 1
    @pageSlider.intValue = @currentPageIndex
  end

  def moveUp(sender)
    moveRight(sender)
  end

end