HotCocoa::Mappings.map :color => :NSColor do
  
  def alloc_with_options(options)
    if options.has_key?(:name)
      color = eval("NSColor.#{options.delete(:name)}Color")
      color = color.colorWithAlphaComponent(options.delete(:alpha)) if options.has_key?(:alpha)
      return color
    elsif options.has_key?(:rgb)
      calibrated = !options.delete(:device)
      rgb = options.delete(:rgb)
      red   = ((rgb >> 16) & 0xff)/255.0
      green = ((rgb >> 8) & 0xff)/255.0
      blue  = (rgb & 0xff)/255.0
      alpha = options.delete(:alpha) || 1.0
      if calibrated
        return NSColor.colorWithCalibratedRed red, green:green, blue:blue, alpha:alpha
      else
        return NSColor.colorWithDeviceRed red, green:green, blue:blue, alpha:alpha
      end
    elsif (options.keys & [:red, :green, :blue]).size == 3
      alpha = (options.delete(:alpha) or 1.0)
      return NSColor.colorWithCalibratedRed options.delete(:red), green:options.delete(:green), blue:options.delete(:blue), alpha:alpha
    else
      raise "Cannot create color with the provided options"
    end
  end
  
end
