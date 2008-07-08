HotCocoa::Mappings.map :image => :NSImage do
  
  def init_with_options(image, options)
    if options.has_key?(:file)
      image.initWithContentsOfFile(options.delete(:file))
    elsif options.has_key?(:url)
      image.initByReferencingURL(NSURL.alloc.initWithString(options.delete(:url)))
    else
      image.init
    end
  end
  
end
