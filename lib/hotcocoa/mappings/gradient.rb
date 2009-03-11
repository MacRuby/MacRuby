HotCocoa::Mappings.map :gradient => :NSGradient do 
  
  def alloc_with_options(options)
    if options[:colors]
      if options[:locations]
        NSGradient.alloc.initWithColors options.delete(:colors), atLocations:options.delete(:locations), colorSpace:(options.delete(:color_space) || NSColorSpace.deviceRGBColorSpace)
      else
        NSGradient.alloc.initWithColors(options.delete(:colors))
      end
    elsif options[:start] && option[:end]
      NSGradient.alloc.initWithStartingColor options.delete(:start), endingColor:options.delete(:end)
    end
  end
  
end