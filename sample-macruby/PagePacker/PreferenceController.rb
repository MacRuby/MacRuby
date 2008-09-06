PaperSizeChangedNotification = 'PaperSizeChangedNotification'
PaperSizeKey = 'PaperSize'
FontFamilyKey = 'FontFamily'
FontSizeKey = 'FontSize'

class PreferenceController < NSWindowController

  attr_accessor :paperPopUp, :textFontField

  attr_reader :textFont

  Defaults = {
    PaperSizeKey => 0,
    FontFamilyKey => 'Helvetica',
    FontSizeKey => 8.0
  }
  NSUserDefaults.standardUserDefaults.registerDefaults Defaults

  def init
    initWithWindowNibName('PreferenceController')
    defaults = NSUserDefaults.standardUserDefaults
    @textFont = NSFont.fontWithName defaults.stringForKey(FontFamilyKey),
    size:defaults.stringForKey(FontSizeKey)
    self
  end

  def self.sharedPreferenceController
    @instance ||= alloc.init
  end
  
  attr_accessor :textFont
  
  def paperSizeID
    NSUserDefaults.standardUserDefaults.integerForKey(PaperSizeKey)
  end
  
  def setPaperSizeID(i)
    NSUserDefaults.standardUserDefaults.setInteger i, forKey:PaperSizeKey
    NSNotificationCenter.defaultCenter.postNotificationName PaperSizeChangedNotification,
      object:self,
      userInfo:{PaperSizeKey => paperSize}
  end
  
  def paperSize
    w, h = 
      if paperSizeID == 0
        [612, 792] # letter
      else
        [595, 842] # A4
      end
    NSMakeSize(w, h)
  end

  def windowDidLoad
    i = paperSizeID
    @paperPopUp.selectItemWithTag i
    @textFontField.stringValue = fontDescription
  end
  
  def paperChosen(sender)
    i = @paperPopUp.selectedTag
    setPaperSizeID(i)
  end

  def setTextFont(f)
    if @textFont != f
      @textFont = f
      ud = NSUserDefaults.standardUserDefaults
      ud.setObject f.familyName, forKey:FontFamilyKey
      ud.setFloat f.pointSize, forKey:FontSizeKey
      @textFontField.stringValue = fontDescription
    end
  end

  def fontDescription
    "%@ - %.1f" % [@textFont.displayName, @textFont.pointSize]
  end

  def changeFont(sender)
    setTextFont sender.convertFont(@textFont)
  end
  
  def chooseFont(sender)
    NSFontManager.sharedFontManager.setSelectedFont @textFont, isMultiple:false
    NSFontPanel.sharedFontPanel.makeKeyAndOrderFront nil
  end
end
