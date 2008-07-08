HotCocoa::Mappings.map :image_view => :NSImageView do
  
  def init_with_options(image_view, options)
    image_view.initWithFrame options.delete(:frame)
  end
  
  custom_methods do
    
    def url=(url)
      setImage(NSImage.alloc.initByReferencingURL(NSURL.alloc.initWithString(url)))
    end
    
    def file=(file)
      setImage(NSImage.alloc.initWithContentsOfFile(file))
    end
    
  end
  
end
