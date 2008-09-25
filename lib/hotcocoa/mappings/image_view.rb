HotCocoa::Mappings.map :image_view => :NSImageView do
  
  defaults :layout => {}, :frame_style => :none
  
  constant :frame_style, {
    :none   => NSImageFrameNone,
    :photo  => NSImageFramePhoto,
    :bezel  => NSImageFrameGrayBezel,
    :groove => NSImageFrameGroove,
    :button => NSImageFrameButton
  }
  
  constant :scale, {
    :fit => NSScaleToFit,
    :none => NSScaleNone,
    :proportionally => NSScaleProportionally
  }
  
  def init_with_options(image_view, options)
    image_view.initWithFrame options.delete(:frame)
  end
  
  custom_methods do
    
    def frame_style=(value)
      setImageFrameStyle(value)
    end
    
    def url=(url)
      setImage(NSImage.alloc.initByReferencingURL(NSURL.alloc.initWithString(url)))
    end
    
    def file=(file)
      setImage(NSImage.alloc.initWithContentsOfFile(file))
    end
    
    def scale=(value)
      setImageScaling(value)
    end
    
  end
  
end
