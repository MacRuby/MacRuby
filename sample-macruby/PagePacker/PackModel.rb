PackModelChangedNotification = 'PackModelChangedNotification'

BLOCK_COUNT = 8

class PackModel
  def initialize
    @pageInfos = Array.new(BLOCK_COUNT)
  end

  def preparedImageRepForPage(pageNum)
    obj = @pageInfos[pageNum]
    if obj == nil
      nil
    else
      obj.preparedImageRep
    end
  end

  def _replacePageInfoAt(i, withPageInfo:pi)
    oldInfo = @pageInfos[i]
    if pi != oldInfo
      obj = @undoManager.prepareWithInvocationTarget self
      obj._replacePageInfoAt i, withPageInfo:oldInfo
      @pageInfos[i] = pi
      NSNotificationCenter.defaultCenter.postNotificationName PackModelChangedNotification,
        object:self, userInfo:nil
    end
  end

  def setImageRep(r, pageOfRep:repPage, forPage:viewPage)
    pi = PageInfo.new
    pi.setImageRep r
    pi.setPageOfRep repPage
    _replacePageInfoAt viewPage, withPageInfo:pi
  end

  def initWithCoder(c)
    super
    @pageInfos = c.decodeObjectForKey('pageInfos')
    self
  end

  def encodeWithCoder(c)
    c.encodeObject @pageInfos, forKey:'pageInfos'
  end

  def setUndoManager(undo)
    @undoManager = undo
  end
  
  attr_reader :undoManager

  def removeAllImageReps
    BLOCK_COUNT.times { |i| _replacePageInfoAt i, withPageInfo:nil }
  end

  def removeImageRepAtPage(i)
    _replacePageInfoAt i, withPageInfo:nil
  end

  def swapImageRepAt(i, withRepAt:j)
    pii = @pageInfos[i]
    pij = @pageInfos[j]
    
    _replacePageInfoAt(i, withPageInfo:pij)
    _replacePageInfoAt(j, withPageInfo:pii)
  end

  def copyImageRepAt(i, toRepAt:j)
    pii = @pageInfos[i]
    pij = PageInfo.new
    pij.setImageRep pii.imageRep
    pij.setPageOfRep pii.pageOfRep
    _replacePageInfoAt j, withPageInfo:pij
  end

  def pageIsFilled(i)
    @pageInfos[i] != nil
  end

  def textAttributes
    { NSFontAttributeName => PreferenceController.sharedPreferenceController.textFont }
  end

  def putAttributedString(attString, startingOnPage:i)
    pdf = pdfFromAttributedStringOfSize attString, NSMakeSize(200, 300)
    putPDFData pdf, startingOnPage:i
  end

  def putPDF(pdf, startingOnPage:i)
    pdf.pageCount.times do |j|
      break if j + i >= BLOCK_COUNT
      setImageRep pdf, pageOfRep:j, forPage:j+i
    end
    i + j
  end

  def putFile(currentPath, startingOnPage:i)
    imageRep = NSImageRep.imageRepWithContentsOfFile currentPath
    if imageRep == nil
      str = NSString.stringWithContentsOfFile currentPath, encoding:NSUTF8StringEncoding, error:nil
      if str == nil
        i
      else
        attString = NSAttributedString.alloc.initWithString str, attributes:textAttributes
        putAttributedString attString, startingOnPage:i
      end
    elsif imageRep.kind_of?(NSPDFImageRep)
      putPDF imageRep, startingOnPage:i
    else
      setImageRep imageRep, pageOfRep:-1, forPage:i
      i + 1
    end
  end

  def putFiles(filenames, startingOnPage:i)
    currentStart = i
    filenames.each { |x| currentStart = putFile(x, startingOnPage:currentStart) }
    currentStart
  end

  def putPDFData(d, startingOnPage:i)
    ir = NSPDFImageRep.alloc.initWithData(d)
    i - 1 + ir.pageCount.times do |j|
      break if j + i >= BLOCK_COUNT
      setImageRep ir, pageOfRep:j, forPage:j + i
    end
  end  
end