class MacRuby
  module ObjcExt
    module NSRect
      # Returns the `x' coordinate of the rects origin instance.
      def x
        origin.x
      end
      
      # Assigns the `x' coordinate on the rects origin instance.
      def x=(x)
        origin.x = coerce_float(x)
      end
      
      # Returns the `y' coordinate of the rects origin instance.
      def y
        origin.y
      end
      
      # Assigns the `y' coordinate on the rects origin instance.
      def y=(y)
        origin.y = coerce_float(y)
      end
      
      # Returns the `height' of the rects size instance.
      def height
        size.height
      end
      
      # Sets the `height' on the rects size instance.
      def height=(height)
        size.height = coerce_float(height)
      end
      
      # Returns the `width' of the rects size instance.
      def width
        size.width
      end
      
      # Sets the `width' on the rects size instance.
      def width=(width)
        size.width = coerce_float(width)
      end
      
      private
      
      # Needed because atm NSCFNumber to Float conversion does not happen yet.
      # In other words, Numeric should be build upon NSCFNumber.
      def coerce_float(value)
        (value.is_a?(NSCFNumber) ? value.floatValue : value)
      end
    end
  end
end

NSRect.send(:include, MacRuby::ObjcExt::NSRect)