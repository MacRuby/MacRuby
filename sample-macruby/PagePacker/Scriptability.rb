class NSObject
  def returnError(n, string:s)
    c = NSScriptCommand.currentCommand
    c.scriptErrorNumber = n
    c.scriptErrorString = s if s
    nil
  end
end

class CatalogController
  attr_reader :pdfView  
end

class AppController
  def application(sender, delegateHandlesKey:key)
    key == 'pageSizePref'
  end

  PAGE_SIZE_CODES = ['psLt', 'psA4'] # FIXME need to convert these to real 4 char codes 
  
  def pageSizePref
    idx = PreferenceController.sharedPreferenceController.paperSizeID
    PAGE_SIZE_CODES[idx]
  end
  
  def setPageSizePref(sz)
    idx = PAGE_SIZE_CODES[idx].index(sz)
    if idx
      PreferenceController.sharedPreferenceController.setPaperSizeID idx
    end
  end
end

class MyDocument

  attr_reader :packModel
  
  def countOfPagesArray; 8; end
  
  def objectInPagesArrayAtIndex(i)
    if i > 7
      returnError errOSACantAccess, string:"No such document page."
    else
      p = MNDocPage.new
      p.index = i
      p.document = self
      p
    end
  end  
  
  def insertInPagesArray(obj)
    returnError errOSACantAssign, string:"Can't create additional document pages."
  end

  def removeObjectFromPagesArrayAtIndex(idx)
    returnError errOSACantAssign, string:"Can't delete document pages."
  end
end

class MNDocPage
  def handleClearScriptCommand(sender)
    document.packModel.removeImageRepAtPage @index
  end
  
  def objectSpecifier
    NSIndexSpecifier.alloc.initWithContainerClassDescription @document.classDescription,
      containerSpecifier:@document.objectSpecifier,
      key:'PagesArray',
      index:@index
  end

  def setCatalogSourcePage(p)
    pp = p - 1
    doc = CatalogController.sharedCatalogController.pdfView.document
    if pp >= doc.pageCount
      returnError errOSACantAccess, string:"No such catalog page."
    else
      page = doc.pageAtIndex(pp)
      d = page.dataRepresentation
      @document.packModel.putPDFData d, startingOnPage:@index
    end
  end

  def catalogSourcePage
    returnError errOSACantAccess, string:"This property is write-only."
  end
  
  def setFileSource(url)
    @document.packModel.putFiles [url.path], startingOnPage:@index
  end

  def fileSource
    returnError errOSACantAccess, string:"This property is write-only."
  end
end