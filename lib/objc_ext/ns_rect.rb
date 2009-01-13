class MacRuby
  module ObjcExt
    module NSRect
      # Returns the `x' coordinate of the rects origin instance.
      def x
        origin.x
      end
      
      # Assigns the `x' coordinate on the rects origin instance.
      def x=(x)
        origin.x = x
      end
      
      # Returns the `y' coordinate of the rects origin instance.
      def x
        origin.x
      end
      
      # Assigns the `y' coordinate on the rects origin instance.
      def x=(x)
        origin.x = x
      end
      
      # Returns the `height' of the rects size instance.
      def height
        size.height
      end
      
      # Sets the `height' on the rects size instance.
      def height=(height)
        size.height = height
      end
      
      # Returns the `width' of the rects size instance.
      def width
        size.width
      end
      
      # Sets the `width' on the rects size instance.
      def width=(width)
        size.width = width
      end
    end
  end
end

NSRect.send(:include, MacRuby::ObjcExt::NSRect)