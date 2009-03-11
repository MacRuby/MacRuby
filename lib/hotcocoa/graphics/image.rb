# Ruby Cocoa Graphics is a graphics library providing a simple object-oriented 
# interface into the power of Mac OS X's Core Graphics and Core Image drawing libraries.  
# With a few lines of easy-to-read code, you can write scripts to draw simple or complex 
# shapes, lines, and patterns, process and filter images, create abstract art or visualize 
# scientific data, and much more.
# 
# Inspiration for this project was derived from Processing and NodeBox.  These excellent 
# graphics programming environments are more full-featured than RCG, but they are implemented 
# in Java and Python, respectively.  RCG was created to offer similar functionality using 
# the Ruby programming language.
#
# Author::    James Reynolds  (mailto:drtoast@drtoast.com)
# Copyright:: Copyright (c) 2008 James Reynolds
# License::   Distributes under the same terms as Ruby

module HotCocoa::Graphics
  
  # load a raw image file for use on a canvas
  class Image

    
    BlendModes = {
      :normal => 'CISourceOverCompositing',
      :multiply => 'CIMultiplyBlendMode',
      :screen => 'CIScreenBlendMode',
      :overlay => 'CIOverlayBlendMode',
      :darken => 'CIDarkenBlendMode',
      :lighten => 'CILightenBlendMode',
      :colordodge => 'CIColorDodgeBlendMode',
      :colorburn => 'CIColorBurnBlendMode',
      :softlight => 'CISoftLightBlendMode',
      :hardlight => 'CIHardLightBlendMode',
      :difference => 'CIDifferenceBlendMode',
      :exclusion => 'CIExclusionBlendMode',
      :hue => 'CIHueBlendMode',
      :saturation => 'CISaturationBlendMode',
      :color => 'CIColorBlendMode',
      :luminosity => 'CILuminosityBlendMode',
      # following modes not available in CGContext:
      :maximum => 'CIMaximumCompositing',
      :minimum => 'CIMinimumCompositing',
      :add => 'CIAdditionCompositing',
      :atop => 'CISourceAtopCompositing',
      :in => 'CISourceInCompositing',
      :out => 'CISourceOutCompositing',
      :over => 'CISourceOverCompositing'
    }
    BlendModes.default('CISourceOverCompositing')
      
    attr_reader :cgimage
  
    # load the image from the given path
    def initialize(img, verbose=false)
      self.verbose(verbose)
      case img
      when String
        # if it's the path to an image file, load as a CGImage
        @path = img
        File.exists?(@path) or raise "ERROR: file not found: #{@path}"

        nsimage = NSImage.alloc.initWithContentsOfFile(img)
        nsdata = nsimage.TIFFRepresentation
        @nsbitmapimage = NSBitmapImageRep.imageRepWithData(nsdata)
        # cgimagesource = CGImageSourceCreateWithData(nsdata) # argh, doesn't work
        @ciimage = CIImage.alloc.initWithBitmapImageRep(@nsbitmapimage)
      when Canvas
        puts "Image.new with canvas" if @verbose
        @path = 'canvas'
        @cgimage = img.cgimage
      when Image
        # copy image?
      else
        raise "ERROR: image type not recognized: #{img.class}"
      end
      # save the original
      @original = @ciimage.copy
      puts "Image.new from [#{@path}] at [#{x},#{y}] with #{width}x#{height}" if @verbose
      self
    end
  
    # reload the bitmap image
    def reset
      @ciimage = CIImage.alloc.initWithBitmapImageRep(@nsbitmapimage)
      self
    end
  
    # set registration mode to :center or :corner
    def registration(mode=:center)
      @registration = mode
    end
  
    # print the parameters of the path
    def to_s
      "image.to_s: #{@path} at [#{x},#{y}] with #{width}x#{height}"
    end
  
    # print drawing functions if verbose is true
    def verbose(tf=true)
      @verbose = tf
    end
  
    # return the width of the image
    def width
      @ciimage ? @ciimage.extent.size.width : CGImageGetWidth(@cgimage)
    end
  
    # return the height of the image
    def height
      @ciimage ? @ciimage.extent.size.height : CGImageGetHeight(@cgimage)
    end
  
    # return the x coordinate of the image's origin
    def x
      @ciimage ? @ciimage.extent.origin.x : 0
    end
  
    # return the y coordinate of the image's origin
    def y
      @ciimage ? @ciimage.extent.origin.y : 0
    end
  
    # def translate(x,y)
    #   matrix = CGAffineTransformMakeTranslation(x,y)
    #   @ciimage = @ciimage.imageByApplyingTransform(matrix)
    #   @ciimage.extent
    #   self
    # end
  
    # RESIZING/MOVING
  
    # scale the image by multiplying the width by a multiplier, optionally scaling height using aspect ratio
    def scale(multiplier, aspect=1.0)    
      puts "image.scale: #{multiplier},#{aspect}" if @verbose
      filter 'CILanczosScaleTransform', :inputScale => multiplier.to_f, :inputAspectRatio => aspect.to_f
      self
    end
  
    # scale image to fit within a box of w,h using CIAffineTransform (sharper)
    def fit2(w, h)
      width_multiplier = w.to_f / width
      height_multiplier = h.to_f / height
      multiplier = width_multiplier < height_multiplier ? width_multiplier : height_multiplier
      puts "image.fit2: #{multiplier}" if @verbose
      transform = NSAffineTransform.transform
      transform.scaleBy(multiplier)
      filter 'CIAffineTransform', :inputTransform => transform
      self
    end
  
    # scale image to fit within a box of w,h using CILanczosScaleTransform
    def fit(w, h)
      # http://gigliwood.com/weblog/Cocoa/Core_Image,_part_2.html
      old_w = self.width.to_f
      old_h = self.height.to_f
      old_x = self.x
      old_y = self.y
    
      # choose a scaling factor
      width_multiplier = w.to_f / old_w
      height_multiplier = h.to_f / old_h
      multiplier = width_multiplier < height_multiplier ? width_multiplier : height_multiplier

      # crop result to integer pixel dimensions
      new_width = (self.width * multiplier).truncate
      new_height = (self.height * multiplier).truncate
    
      puts "image.fit: old size #{old_w}x#{old_h}, max target #{w}x#{h}, multiplier #{multiplier}, new size #{new_width}x#{new_height}" if @verbose
      clamp
      scale(multiplier)
      crop(old_x, old_y, new_width, new_height)
      #origin(:bottomleft)
      self
    end

    # resize the image to have new dimensions w,h
    def resize(w, h)
      oldw = width
      oldh = height
      puts "image.resize #{oldw}x#{oldh} => #{w}x#{h}" if @verbose
      width_ratio = w.to_f / oldw.to_f
      height_ratio = h.to_f / oldh.to_f
      aspect = width_ratio / height_ratio  # (works when stretching tall, gives aspect = 0.65)
      scale(height_ratio,aspect)
      origin(:bottomleft)
      self
    end
  
    # crop the image to a rectangle from x1,y2 with width x height
    def crop(x=nil,y=nil,w=nil,h=nil)
    
      # crop to largest square if no parameters were given
      unless x
        if (self.width > self.height)
          side = self.height
          x = (self.width - side) / 2
          y = 0
        else
          side = self.width
          y = (self.height - side) / 2
          x = 0
        end
        w = h = side
      end
    
      puts "image.crop [#{x},#{y}] with #{w},#{h}" if @verbose
      #vector = CIVector.vectorWithX_Y_Z_W(x.to_f,y.to_f,w.to_f,h.to_f)
      vector = CIVector.vectorWithX x.to_f, Y:y.to_f, Z:w.to_f, W:h.to_f
      filter('CICrop', :inputRectangle => vector)
      origin(:bottomleft)
      self
    end
  
    # apply an affine transformation using matrix parameters a,b,c,d,tx,ty
    def transform(a, b, c, d, tx, ty)
      puts "image.transform #{a},#{b},#{c},#{d},#{tx},#{ty}" if @verbose
      transform = CGAffineTransformMake(a, b, c, d, tx, ty) # FIXME: needs to be NSAffineTransform?
      filter 'CIAffineTransform', :inputTransform => transform
      self
    end
  
    # translate image by tx,ty
    def translate(tx, ty)
      puts "image.translate #{tx},#{ty}" if @verbose
      #transform = CGAffineTransformMakeTranslation(tx,ty);
      transform = NSAffineTransform.transform
      transform.translateXBy tx, yBy:ty
      filter 'CIAffineTransform', :inputTransform => transform
      self
    end
  
    # rotate image by degrees
    def rotate(deg)
      puts "image.rotate #{deg}" if @verbose
      #transform = CGAffineTransformMakeRotation(radians(deg));
      transform = NSAffineTransform.transform
      transform.rotateByDegrees(-deg)
      filter 'CIAffineTransform', :inputTransform => transform
      self
    end
    
    # set the origin to the specified location (:center, :bottomleft, etc)
    def origin(location=:bottomleft)
      movex, movey = reorient(x, y, width, height, location)
      translate(movex, movey)
    end
  
  
    # FILTERS
  
    # apply a crystallizing effect with pixel radius 1-100
    def crystallize(radius=20.0)
      filter 'CICrystallize', :inputRadius => radius
      self
    end
  
    # apply a gaussian blur with pixel radius 1-100
    def blur(radius=10.0)
      filter 'CIGaussianBlur', :inputRadius => inrange(radius, 1.0, 100.0)
      self
    end
  
    # sharpen the image given a radius (0-100) and intensity factor
    def sharpen(radius=2.50, intensity=0.50)
      filter 'CIUnsharpMask', :inputRadius => radius, :inputIntensity => intensity
      self
    end

    # apply a gaussian blur with pixel radius 1-100
    def motionblur(radius=10.0, angle=90.0)
      oldx, oldy, oldw, oldh = [x, y, width, height]
      clamp
      filter 'CIMotionBlur', :inputRadius => radius, :inputAngle => radians(angle)
      crop(oldx, oldy, oldw, oldh)
      self
    end

    # rotate pixels around x,y with radius and angle
    def twirl(x=0, y=0, radius=300, angle=90.0)
      filter 'CITwirlDistortion', :inputCenter => CIVector.vectorWithX(x, Y:y), :inputRadius => radius, :inputAngle => radians(angle)
      self
    end

    # apply a bloom effect
    def bloom(radius=10, intensity=1.0)
      filter 'CIBloom', :inputRadius => inrange(radius, 0, 100), :inputIntensity => inrange(intensity, 0.0, 1.0)
      self
    end

    # adjust the hue of the image by rotating the color wheel from 0 to 360 degrees
    def hue(angle=180)
      filter 'CIHueAdjust', :inputAngle => radians(angle)
      self
    end

    # remap colors so they fall within shades of a single color
    def monochrome(color=Color.gray)
      filter 'CIColorMonochrome', :inputColor => CIColor.colorWithRed(color.r, green:color.g, blue:color.b, alpha:color.a)
      self
    end
  
    # adjust the reference white point for an image and maps all colors in the source using the new reference
    def whitepoint(color=Color.white.ish)
      filter 'CIWhitePointAdjust', :inputColor => CIColor.colorWithRed(color.r, green:color.g, blue:color.b, alpha:color.a)
      self
    end
  
    # reduce colors with a banding effect
    def posterize(levels=6.0)
      filter 'CIColorPosterize', :inputLevels => inrange(levels, 1.0, 300.0)
      self
    end
  
    # detect edges
    def edges(intensity=1.0)
      filter 'CIEdges', :inputIntensity => inrange(intensity, 0.0,10.0)
      self
    end
  
    # apply woodblock-like effect
    def edgework(radius=1.0)
      filter 'CIEdgeWork', :inputRadius => inrange(radius, 0.0,20.0)
      self
    end
  
    # adjust exposure by f-stop
    def exposure(ev=0.5)
      filter 'CIExposureAdjust', :inputEV => inrange(ev, -10.0, 10.0)
      self
    end
  
    # adjust saturation
    def saturation(value=1.5)
      filter 'CIColorControls', :inputSaturation => value, :inputBrightness => 0.0, :inputContrast => 1.0
      self
    end
  
    # adjust brightness (-1 to 1)
    def brightness(value=1.1)
      filter 'CIColorControls', :inputSaturation => 1.0, :inputBrightness => inrange(value, -1.0, 1.0), :inputContrast => 1.0
      self
    end
  
    # adjust contrast (0 to 4)
    def contrast(value=1.5)
      #value = inrange(value,0.25,100.0)
      filter 'CIColorControls', :inputSaturation => 1.0, :inputBrightness => 0.0, :inputContrast => value
      self
    end
  
    # fill with a gradient from color0 to color1 from [x0,y0] to [x1,y1]
    def gradient(color0, color1, x0 = x / 2, y0 = y, x1 = x / 2, y1 = height)
      filter 'CILinearGradient',
        :inputColor0 => color0.rgb, 
        :inputColor1 => color1.rgb, 
        :inputPoint0 => CIVector.vectorWithX(x0, Y:y0), 
        :inputPoint1 => CIVector.vectorWithX(x1, Y:y1)
      self
    end
  
    # use the gray values of the input image as a displacement map (doesn't work with PNG?)
    def displacement(image, scale=50.0)
      filter 'CIDisplacementDistortion', :inputDisplacementImage => image.ciimage, :inputScale => inrange(scale, 0.0, 200.0)
      self
    end
  
    # simulate a halftone screen given a center point, angle(0-360), width(1-50), and sharpness(0-1)
    def dotscreen(dx=0, dy=0, angle=0, width=6, sharpness=0.7)
      filter 'CIDotScreen',
        :inputCenter => CIVector.vectorWithX(dx.to_f, Y:dy.to_f), 
        :inputAngle => max(0, min(angle, 360)), 
        :inputWidth => max(1, min(width, 50)), 
        :inputSharpness => max(0, min(sharpness, 1))
      self
    end
  
    # extend pixels at edges to infinity for nicer sharpen/blur effects
    def clamp
      puts "image.clamp" if @verbose
      filter 'CIAffineClamp', :inputTransform => NSAffineTransform.transform
      self
    end
  
    # blend with the given image using mode (:add, etc)
    def blend(image, mode)
      case image
      when String
        ciimage_background = CIImage.imageWithContentsOfURL(NSURL.fileURLWithPath(image))
      when Image
        ciimage_background = image.ciimage
      else
        raise "ERROR: Image: type not recognized"
      end
      filter BlendModes[mode], :inputBackgroundImage => ciimage_background
      self
    end
  
    # draw this image to the specified context
    def draw(ctx,x=0,y=0,w=width,h=height)
      ciimage
      # imgx = x + self.x
      # imyy = y + self.y
      resize(w, h) if w != self.width || h != self.height
    
      # add the ciimage's own origin coordinates to the target point
      x = x + self.x
      y = y + self.y
    
      puts "image.draw #{x},#{y} #{w}x#{h}" if @verbose
      cicontext = CIContext.contextWithCGContext ctx, options:nil
      #cicontext.drawImage_atPoint_fromRect(ciimage, [x,y], CGRectMake(self.x,self.y,w,h))
      cicontext.drawImage ciimage, atPoint:CGPointMake(x,y), fromRect:CGRectMake(self.x,self.y,w,h)
    end
  
    # return the CIImage for this Image object
    def ciimage
      @ciimage ||= CIImage.imageWithCGImage(@cgimage)
    end
  
    # return an array of n colors chosen randomly from the source image.
    # if type = :grid, choose average colors from each square in a grid with n squares
    def colors(n=32, type=:random)
      ciimage
      colors = []
      if (type == :grid) then
        filtername = 'CIAreaAverage'
        f = CIFilter.filterWithName(filtername)
        f.setDefaults
        f.setValue_forKey(@ciimage, 'inputImage')
    
        extents = []

        rows = Math::sqrt(n).truncate
        cols = rows
        w = self.width
        h = self.height
        block_width = w / cols
        block_height = h / rows
        rows.times do |row|
          cols.times do |col|
            x = col * block_width
            y = row * block_height
            extents.push([x, y, block_width, block_height])
          end
        end
        extents.each do |extent|
          x, y, w, h = extent
          extent = CIVector.vectorWithX x.to_f, Y:y.to_f, Z:w.to_f, W:h.to_f
          f.setValue_forKey(extent, 'inputExtent')
          ciimage = f.valueForKey('outputImage') # CIImageRef
          nsimg = NSBitmapImageRep.alloc.initWithCIImage(ciimage)
          nscolor = nsimg.colorAtX 0, y:0 # NSColor
          #r,g,b,a = nscolor.getRed_green_blue_alpha_()
          r,b,b,a = [nscolor.redComponent,nscolor.greenComponent,nscolor.blueComponent,nscolor.alphaComponent]
          colors.push(Color.new(r,g,b,1.0))
        end
      elsif (type == :random)
        nsimg = NSBitmapImageRep.alloc.initWithCIImage(@ciimage)
        n.times do |i|
          x = rand(self.width)
          y = rand(self.height)
          nscolor = nsimg.colorAtX x, y:y # NSColor
          #r,g,b,a = nscolor.getRed_green_blue_alpha_()
          r,g,b,a = [nscolor.redComponent,nscolor.greenComponent,nscolor.blueComponent,nscolor.alphaComponent]
          colors.push(Color.new(r,g,b,1.0))
        end
      end
      colors
    end
  
    private
  
      # apply the named CoreImage filter using a hash of parameters
      def filter(filtername, parameters)
        ciimage
        f = CIFilter.filterWithName(filtername)
        f.setDefaults
        f.setValue @ciimage, forKey:'inputImage'
        parameters.each do |key,value|
          f.setValue value, forKey:key
        end
        puts "image.filter #{filtername}" if @verbose
        @ciimage = f.valueForKey('outputImage') # CIImageRef
        self
      end
  
  end
  
end
