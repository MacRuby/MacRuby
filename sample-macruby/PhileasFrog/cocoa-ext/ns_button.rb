class NSButton

  # wrapper to let you easily change the font size of a button's title
  def title_font_size=(size)
    color =  self.attributedTitle.attribute( NSForegroundColorAttributeName, atIndex: 0, effectiveRange: nil)
    current_font = self.attributedTitle.attribute( NSFontAttributeName, atIndex: 0, effectiveRange: nil)
    font = NSFont.fontWithName(current_font.fontName, size:size)
      
    opts = { NSForegroundColorAttributeName => color, NSFontAttributeName => font }
    self.attributedTitle = NSAttributedString.alloc.initWithString( self.title, attributes: opts)
  end
  
  def title_color=(color)
    current_font = self.attributedTitle.attribute( NSFontAttributeName, atIndex: 0, effectiveRange: nil)
    opts = { NSForegroundColorAttributeName => color, NSFontAttributeName => current_font }
    self.attributedTitle = NSAttributedString.alloc.initWithString( self.title, attributes: opts)
  end
  
end