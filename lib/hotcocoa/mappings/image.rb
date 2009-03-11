HotCocoa::Mappings.map :image => :NSImage do
  
  def alloc_with_options(options)
    if options.has_key?(:file)
      NSImage.alloc.initWithContentsOfFile(options.delete(:file))
    elsif options.has_key?(:url)
      NSImage.alloc.initByReferencingURL(NSURL.alloc.initWithString(options.delete(:url)))
    elsif options.has_key?(:named)
      NSImage.imageNamed(options.delete(:named))
    else
      NSImage.alloc.init
    end
  end

end
