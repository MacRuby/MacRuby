HotCocoa::Mappings.map :font => :NSFont do
  
  constant :trait, {
     :italic => NSItalicFontMask,
     :bold => NSBoldFontMask,
     :unbold => NSUnboldFontMask,
     :nonstandard_character_set => NSNonStandardCharacterSetFontMask,
     :narrow => NSNarrowFontMask,
     :expanded => NSExpandedFontMask,
     :condensed => NSCondensedFontMask,
     :small_caps => NSSmallCapsFontMask,
     :poster => NSPosterFontMask,
     :compressed => NSCompressedFontMask,
     :fixed_pitch => NSFixedPitchFontMask,
     :unitalic => NSUnitalicFontMask
  }
  
  def alloc_with_options(options)
    font = nil
    {
      :label => :labelFontOfSize,
      :system => :systemFontOfSize,
      :control_content => :controlContentFontOfSize,
      :menu_bar => :menuBarFontOfSize,
      :message => :messageFontOfSize,
      :palette =>  :paletteFontOfSize,
      :small_system => :smallSystemFontOfSize,
      :title_bar => :titleBarFontOfSize,
      :tool_tip => :toolTipFontOfSize,
      :user_fixed => :userFixedPitchFontOfSize,
      :user => :userFontOfSize
    }.each do |key, method|
      if options.has_key?(key)
        font = eval("NSFont.#{method}(#{options.delete(key)})")
        break
      end
    end
    font = NSFont.fontWithName(options.delete(:name), size:(options.delete(:size) || 0)) if options.has_key?(:name)
    raise "Cannot create font with the provided options" unless font
    font = NSFontManager.sharedFontManager.convertFont(font, toHaveTrait:options.delete(:trait)) if options[:trait]
    font
  end
  
end
