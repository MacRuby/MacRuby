HotCocoa::Mappings.map :font => :NSFont do
  
  def alloc_with_options(options)
    if options.has_key?(:system) && options.delete(:bold)
      return NSFont.boldSystemFontOfSize(options.delete(:system))
    end
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
      return eval("NSFont.#{method}(#{options.delete(key)})") if options.has_key?(key)
    end
    if options.has_key?(:name)
      return NSFont.fontWithName(options.delete(:name), size:(options.delete(:size) || 0))
    else
      raise "Cannot create font with the provided options"
    end
  end
  
end
