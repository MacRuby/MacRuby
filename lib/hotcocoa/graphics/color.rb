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

  # define and manipulate colors in RGBA format
  class Color
  
    # License: GPL - includes ports of some code by Tom De Smedt, Frederik De Bleser
  
    #attr_accessor :r,:g,:b,:a
    attr_accessor :rgb
    # def initialize(r=0.0,g=0.0,b=0.0,a=1.0)
    #   @c = CGColorCreate(@colorspace, [r,g,b,a])
    # end
  
    # create a new color with the given RGBA values
    def initialize(r=0.0, g=0.0, b=1.0, a=1.0)
      @nsColor = NSColor.colorWithDeviceRed r, green:g, blue:b, alpha:a
      @rgb = @nsColor.colorUsingColorSpaceName NSDeviceRGBColorSpace
      self
    end
  
    COLORNAMES = {
        "lightpink"            => [1.00, 0.71, 0.76],
        "pink"                 => [1.00, 0.75, 0.80],
        "crimson"              => [0.86, 0.08, 0.24],
        "lavenderblush"        => [1.00, 0.94, 0.96],
        "palevioletred"        => [0.86, 0.44, 0.58],
        "hotpink"              => [1.00, 0.41, 0.71],
        "deeppink"             => [1.00, 0.08, 0.58],
        "mediumvioletred"      => [0.78, 0.08, 0.52],
        "orchid"               => [0.85, 0.44, 0.84],
        "thistle"              => [0.85, 0.75, 0.85],
        "plum"                 => [0.87, 0.63, 0.87],
        "violet"               => [0.93, 0.51, 0.93],
        "fuchsia"              => [1.00, 0.00, 1.00],
        "darkmagenta"          => [0.55, 0.00, 0.55],
        "purple"               => [0.50, 0.00, 0.50],
        "mediumorchid"         => [0.73, 0.33, 0.83],
        "darkviolet"           => [0.58, 0.00, 0.83],
        "darkorchid"           => [0.60, 0.20, 0.80],
        "indigo"               => [0.29, 0.00, 0.51],
        "blueviolet"           => [0.54, 0.17, 0.89],
        "mediumpurple"         => [0.58, 0.44, 0.86],
        "mediumslateblue"      => [0.48, 0.41, 0.93],
        "slateblue"            => [0.42, 0.35, 0.80],
        "darkslateblue"        => [0.28, 0.24, 0.55],
        "ghostwhite"           => [0.97, 0.97, 1.00],
        "lavender"             => [0.90, 0.90, 0.98],
        "blue"                 => [0.00, 0.00, 1.00],
        "mediumblue"           => [0.00, 0.00, 0.80],
        "darkblue"             => [0.00, 0.00, 0.55],
        "navy"                 => [0.00, 0.00, 0.50],
        "midnightblue"         => [0.10, 0.10, 0.44],
        "royalblue"            => [0.25, 0.41, 0.88],
        "cornflowerblue"       => [0.39, 0.58, 0.93],
        "lightsteelblue"       => [0.69, 0.77, 0.87],
        "lightslategray"       => [0.47, 0.53, 0.60],
        "slategray"            => [0.44, 0.50, 0.56],
        "dodgerblue"           => [0.12, 0.56, 1.00],
        "aliceblue"            => [0.94, 0.97, 1.00],
        "steelblue"            => [0.27, 0.51, 0.71],
        "lightskyblue"         => [0.53, 0.81, 0.98],
        "skyblue"              => [0.53, 0.81, 0.92],
        "deepskyblue"          => [0.00, 0.75, 1.00],
        "lightblue"            => [0.68, 0.85, 0.90],
        "powderblue"           => [0.69, 0.88, 0.90],
        "cadetblue"            => [0.37, 0.62, 0.63],
        "darkturquoise"        => [0.00, 0.81, 0.82],
        "azure"                => [0.94, 1.00, 1.00],
        "lightcyan"            => [0.88, 1.00, 1.00],
        "paleturquoise"        => [0.69, 0.93, 0.93],
        "aqua"                 => [0.00, 1.00, 1.00],
        "darkcyan"             => [0.00, 0.55, 0.55],
        "teal"                 => [0.00, 0.50, 0.50],
        "darkslategray"        => [0.18, 0.31, 0.31],
        "mediumturquoise"      => [0.28, 0.82, 0.80],
        "lightseagreen"        => [0.13, 0.70, 0.67],
        "turquoise"            => [0.25, 0.88, 0.82],
        "aquamarine"           => [0.50, 1.00, 0.83],
        "mediumaquamarine"     => [0.40, 0.80, 0.67],
        "mediumspringgreen"    => [0.00, 0.98, 0.60],
        "mintcream"            => [0.96, 1.00, 0.98],
        "springgreen"          => [0.00, 1.00, 0.50],
        "mediumseagreen"       => [0.24, 0.70, 0.44],
        "seagreen"             => [0.18, 0.55, 0.34],
        "honeydew"             => [0.94, 1.00, 0.94],
        "darkseagreen"         => [0.56, 0.74, 0.56],
        "palegreen"            => [0.60, 0.98, 0.60],
        "lightgreen"           => [0.56, 0.93, 0.56],
        "limegreen"            => [0.20, 0.80, 0.20],
        "lime"                 => [0.00, 1.00, 0.00],
        "forestgreen"          => [0.13, 0.55, 0.13],
        "green"                => [0.00, 0.50, 0.00],
        "darkgreen"            => [0.00, 0.39, 0.00],
        "lawngreen"            => [0.49, 0.99, 0.00],
        "chartreuse"           => [0.50, 1.00, 0.00],
        "greenyellow"          => [0.68, 1.00, 0.18],
        "darkolivegreen"       => [0.33, 0.42, 0.18],
        "yellowgreen"          => [0.60, 0.80, 0.20],
        "olivedrab"            => [0.42, 0.56, 0.14],
        "ivory"                => [1.00, 1.00, 0.94],
        "beige"                => [0.96, 0.96, 0.86],
        "lightyellow"          => [1.00, 1.00, 0.88],
        "lightgoldenrodyellow" => [0.98, 0.98, 0.82],
        "yellow"               => [1.00, 1.00, 0.00],
        "olive"                => [0.50, 0.50, 0.00],
        "darkkhaki"            => [0.74, 0.72, 0.42],
        "palegoldenrod"        => [0.93, 0.91, 0.67],
        "lemonchiffon"         => [1.00, 0.98, 0.80],
        "khaki"                => [0.94, 0.90, 0.55],
        "gold"                 => [1.00, 0.84, 0.00],
        "cornsilk"             => [1.00, 0.97, 0.86],
        "goldenrod"            => [0.85, 0.65, 0.13],
        "darkgoldenrod"        => [0.72, 0.53, 0.04],
        "floralwhite"          => [1.00, 0.98, 0.94],
        "oldlace"              => [0.99, 0.96, 0.90],
        "wheat"                => [0.96, 0.87, 0.07],
        "orange"               => [1.00, 0.65, 0.00],
        "moccasin"             => [1.00, 0.89, 0.71],
        "papayawhip"           => [1.00, 0.94, 0.84],
        "blanchedalmond"       => [1.00, 0.92, 0.80],
        "navajowhite"          => [1.00, 0.87, 0.68],
        "antiquewhite"         => [0.98, 0.92, 0.84],
        "tan"                  => [0.82, 0.71, 0.55],
        "burlywood"            => [0.87, 0.72, 0.53],
        "darkorange"           => [1.00, 0.55, 0.00],
        "bisque"               => [1.00, 0.89, 0.77],
        "linen"                => [0.98, 0.94, 0.90],
        "peru"                 => [0.80, 0.52, 0.25],
        "peachpuff"            => [1.00, 0.85, 0.73],
        "sandybrown"           => [0.96, 0.64, 0.38],
        "chocolate"            => [0.82, 0.41, 0.12],
        "saddlebrown"          => [0.55, 0.27, 0.07],
        "seashell"             => [1.00, 0.96, 0.93],
        "sienna"               => [0.63, 0.32, 0.18],
        "lightsalmon"          => [1.00, 0.63, 0.48],
        "coral"                => [1.00, 0.50, 0.31],
        "orangered"            => [1.00, 0.27, 0.00],
        "darksalmon"           => [0.91, 0.59, 0.48],
        "tomato"               => [1.00, 0.39, 0.28],
        "salmon"               => [0.98, 0.50, 0.45],
        "mistyrose"            => [1.00, 0.89, 0.88],
        "lightcoral"           => [0.94, 0.50, 0.50],
        "snow"                 => [1.00, 0.98, 0.98],
        "rosybrown"            => [0.74, 0.56, 0.56],
        "indianred"            => [0.80, 0.36, 0.36],
        "red"                  => [1.00, 0.00, 0.00],
        "brown"                => [0.65, 0.16, 0.16],
        "firebrick"            => [0.70, 0.13, 0.13],
        "darkred"              => [0.55, 0.00, 0.00],
        "maroon"               => [0.50, 0.00, 0.00],
        "white"                => [1.00, 1.00, 1.00],
        "whitesmoke"           => [0.96, 0.96, 0.96],
        "gainsboro"            => [0.86, 0.86, 0.86],
        "lightgrey"            => [0.83, 0.83, 0.83],
        "silver"               => [0.75, 0.75, 0.75],
        "darkgray"             => [0.66, 0.66, 0.66],
        "gray"                 => [0.50, 0.50, 0.50],
        "grey"                 => [0.50, 0.50, 0.50],
        "dimgray"              => [0.41, 0.41, 0.41],
        "dimgrey"              => [0.41, 0.41, 0.41],
        "black"                => [0.00, 0.00, 0.00],
        "cyan"                 => [0.00, 0.68, 0.94],
        #"transparent"          => [0.00, 0.00, 0.00, 0.00],
        "bark"                 => [0.25, 0.19, 0.13]
    }
    
    RYBWheel = [
      [  0,   0], [ 15,   8],
      [ 30,  17], [ 45,  26],
      [ 60,  34], [ 75,  41],
      [ 90,  48], [105,  54],
      [120,  60], [135,  81],
      [150, 103], [165, 123],
      [180, 138], [195, 155],
      [210, 171], [225, 187],
      [240, 204], [255, 219],
      [270, 234], [285, 251],
      [300, 267], [315, 282],
      [330, 298], [345, 329],
      [360, 0  ]
    ]
      
    # create a color with the specified name
    def self.named(name)
      if COLORNAMES[name]
        r, g, b = COLORNAMES[name]
        #puts "matched name #{name}"
        color = Color.new(r, g, b, 1.0)
      elsif name.match(/^(dark|deep|light|bright)?(.*?)(ish)?$/)
        #puts "matched #{$1}-#{$2}-#{$3}"
        value = $1
        color_name = $2
        ish = $3
        analogval = value ? 0 : 0.1
        r, g, b = COLORNAMES[color_name] || [0.0, 0.0, 0.0]
        color = Color.new(r, g, b, 1.0)
        color = c.analog(20, analogval) if ish
        color.lighten(0.2) if value and value.match(/light|bright/)
        color.darken(0.2) if value and value.match(/dark|deep/)
      else
        color = Color.black
      end
      color
    end
  
    # return the name of the nearest named color
    def name
      nearest, d = ["", 1.0]
      red = r
      green = g
      blue = b
      for hue in COLORNAMES.keys
        rdiff = (red - COLORNAMES[hue][0]).abs
        gdiff = (green - COLORNAMES[hue][1]).abs
        bdiff = (blue - COLORNAMES[hue][2]).abs
        totaldiff = rdiff + gdiff + bdiff
        if (totaldiff < d)
          nearest, d = [hue, totaldiff]
        end
      end
      nearest
    end
  
    # if the method name is not recognized, try creating a color with that name
    def self.method_missing(name, *args)
      Color.named(name.to_s.downcase)
    end
  
    # return a copy of this color
    def copy
      Color.new(r, g, b, a)
    end
  
    # print the color's component values
    def to_s
      "color: #{name} (#{r} #{g} #{b} #{a})"
    end
  
    # sort the color by brightness in an array
    def <=> othercolor
       self.brightness <=> othercolor.brightness || self.hue <=> othercolor.hue
    end
  
    # set or retrieve the red component
    def r(val=nil)
      if val
        r, g, b, a = get_rgb
        set_rgb(val, g, b, a)
        self
      else
        @rgb.redComponent
      end
    end
  
    # set or retrieve the green component
    def g(val=nil)
      if val
        r, g, b, a = get_rgb
        set_rgb(r, val, b, a)
        self
      else
        @rgb.greenComponent
      end
    end
  
    # set or retrieve the blue component
    def b(val=nil)
      if val
        r, g, b, a = get_rgb
        set_rgb(r, g, val, a)
        self
      else
        @rgb.blueComponent
      end
    end
  
    # set or retrieve the alpha component
    def a(val=nil)
      if val
        r, g, b, a = get_rgb
        set_rgb(r, g, b, val)
        self
      else
        @rgb.alphaComponent
      end
    end
  
    # set or retrieve the hue
    def hue(val=nil)
      if val
        h, s, b, a = get_hsb
        set_hsb(val, s, b, a)
        self
      else
        @rgb.hueComponent
      end
    end
  
    # set or retrieve the saturation
    def saturation(val=nil)
      if val
        h, s, b, a = get_hsb
        set_hsb(h, val, b, a)
        self
      else
        @rgb.saturationComponent
      end
    end
  
    # set or retrieve the brightness
    def brightness(val=nil)
      if val
        h, s, b, a = get_hsb
        set_hsb(h, s, val, a)
        self
      else
        @rgb.brightnessComponent
      end
    end
  
    # decrease saturation by the specified amount
    def desaturate(step=0.1)
      saturation(saturation - step)
      self
    end
  
    # increase the saturation by the specified amount
    def saturate(step=0.1)
      saturation(saturation + step)
      self
    end
  
    # decrease the brightness by the specified amount
    def darken(step=0.1)
      brightness(brightness - step)
      self
    end
  
    # increase the brightness by the specified amount
    def lighten(step=0.1)
      brightness(brightness + step)
      self
    end
  
    # set the R,G,B,A values
    def set(r, g, b, a=1.0)
      set_rgb(r, g, b, a)
      self
    end
  
    # adjust the Red, Green, Blue, Alpha values by the specified amounts
    def adjust_rgb(r=0.0, g=0.0, b=0.0, a=0.0)
      r0, g0, b0, a0 = get_rgb
      set_rgb(r0+r, g0+g, b0+b, a0+a)
      self
    end
  
    # return RGBA values
    def get_rgb
      #@rgb.getRed_green_blue_alpha_()
      [@rgb.redComponent, @rgb.greenComponent, @rgb.blueComponent, @rgb.alphaComponent]
    end
  
    # set color using RGBA values
    def set_rgb(r, g, b, a=1.0)
      @rgb = NSColor.colorWithDeviceRed r, green:g, blue:b, alpha:a
      self
    end
  
    # return HSBA values
    def get_hsb
      #@rgb.getHue_saturation_brightness_alpha_()
      [@rgb.hueComponent, @rgb.saturationComponent, @rgb.brightnessComponent, @rgb.alphaComponent]
    end
  
    # set color using HSBA values
    def set_hsb(h,s,b,a=1.0)
      @rgb = NSColor.colorWithDeviceHue h, saturation:s, brightness:b, alpha:a
      self
    end
  
    # adjust Hue, Saturation, Brightness, and Alpha by specified amounts
    def adjust_hsb(h=0.0, s=0.0, b=0.0, a=0.0)
      h0, s0, b0, a0 = get_hsb
      set_hsb(h0+h, s0+s, b0+b, a0+a)
      self
    end
  
    # alter the color by the specified random scaling factor
    # def ish(angle=10.0,d=0.02)
    #   # r,g,b,a = get_rgb
    #   # r = vary(r, variance)
    #   # g = vary(g, variance)
    #   # b = vary(b, variance)
    #   # a = vary(a, variance)
    #   # set_rgb(r,g,b,a)
    #   analog(angle,d)
    #   self
    # end
  
    # create a random color
    def random
      set_rgb(rand, rand, rand, 1.0)
      self
    end
  
    # rotate the color on the artistic RYB color wheel (0 to 360 degrees)
    def rotate_ryb(angle=180)

      # An artistic color wheel has slightly different opposites
      # (e.g. purple-yellow instead of purple-lime).
      # It is mathematically incorrect but generally assumed
      # to provide better complementary colors.
      #   
      # http://en.wikipedia.org/wiki/RYB_color_model

      h = hue * 360
      angle = angle % 360.0
      a = 0

      # Approximation of Itten's RYB color wheel.
      # In HSB, colors hues range from 0-360.
      # However, on the artistic color wheel these are not evenly distributed. 
      # The second tuple value contains the actual distribution.

      # Given a hue, find out under what angle it is
      # located on the artistic color wheel.
      (RYBWheel.size-1).times do |i|
        x0,y0 = RYBWheel[i]    
        x1,y1 = RYBWheel[i+1]
        y1 += 360 if y1 < y0
        if y0 <= h && h <= y1
          a = 1.0 * x0 + (x1-x0) * (h-y0) / (y1-y0)
          break
        end
      end
    
      # And the user-given angle (e.g. complement).
      a = (a+angle) % 360

      # For the given angle, find out what hue is
      # located there on the artistic color wheel.
      (RYBWheel.size-1).times do |i|
        x0,y0 = RYBWheel[i]    
        x1,y1 = RYBWheel[i+1]
        y1 += 360 if y1 < y0
        if x0 <= a && a <= x1
          h = 1.0 * y0 + (y1-y0) * (a-x0) / (x1-x0)
          break
        end
      end
    
      h = h % 360
      set_hsb(h/360, self.saturation, self.brightness, self.a)
      self
    end
  
    # rotate the color on the RGB color wheel (0 to 360 degrees)
    def rotate_rgb(angle=180)
      hue = (self.hue + 1.0 * angle / 360) % 1
      set_hsb(hue, self.saturation, self.brightness, @a)
      self
    end

    # return a similar color, varying the hue by angle (0-360) and brightness/saturation by d
    def analog(angle=20, d=0.5)
      c = self.copy
      c.rotate_ryb(angle * (rand*2-1))
      c.lighten(d * (rand*2-1))
      c.saturate(d * (rand*2-1))
    end
  
    # randomly vary the color within a maximum hue range, saturation range, and brightness range
    def drift(maxhue=0.1,maxsat=0.3,maxbright=maxsat)
      # save original values the first time
      @original_hue ||= self.hue
      @original_saturation ||= self.saturation
      @original_brightness ||= self.brightness
      # get current values
      current_hue = self.hue
      current_saturation = self.saturation
      current_brightness = self.brightness
      # generate new values
      randhue = ((rand * maxhue) - maxhue/2.0) + current_hue
      randhue = inrange(randhue, (@original_hue - maxhue/2.0),(@original_hue + maxhue/2.0))
      randsat = (rand * maxsat) - maxsat/2.0 + current_saturation
      randsat = inrange(randsat, @original_saturation - maxsat/2.0,@original_saturation + maxsat/2.0)
      randbright = (rand * maxbright) - maxbright/2.0 + current_brightness
      randbright = inrange(randbright, @original_brightness - maxbright/2.0,@original_brightness + maxbright/2.0)
      # assign new values
      self.hue(randhue)
      self.saturation(randsat)
      self.brightness(randbright)
      self
    end
  
    # convert to the complementary color (the color at opposite on the artistic color wheel)
    def complement
      rotate_ryb(180)
      self
    end

    # blend with another color (doesn't work?)
    # def blend(color, pct=0.5)
    #   blended = NSColor.blendedColorWithFraction_ofColor(pct,color.rgb)
    #   @rgb = blended.colorUsingColorSpaceName(NSDeviceRGBColorSpace)
    #   self
    # end
  
    # create a new RGBA color
    def self.rgb(r, g, b, a=1.0)
      Color.new(r,g,b,a)
    end
  
    # create a new HSBA color
    def self.hsb(h, s, b, a=1.0)
      Color.new.set_hsb(h,s,b,a)
    end
  
    # create a new gray color with the specified darkness
    def self.gray(pct=0.5)
      Color.new(pct,pct,pct,1.0)
    end
  
    # return a random color
    def self.random
      Color.new.random
    end

    # Returns a list of complementary colors. The complement is the color 180 degrees across the artistic RYB color wheel.
    # 1) ORIGINAL: the original color
    # 2) CONTRASTING: same hue as original but much darker or lighter
    # 3) SOFT SUPPORTING: same hue but lighter and less saturated than the original
    # 4) CONTRASTING COMPLEMENT: a much brighter or darker version of the complement hue
    # 5) COMPLEMENT: the hue 180 degrees opposite on the RYB color wheel with same brightness/saturation
    # 6) LIGHT SUPPORTING COMPLEMENT VARIANT: a lighter less saturated version of the complement hue
    def complementary
      colors = []
    
      # A contrasting color: much darker or lighter than the original.
      colors.push(self)
      c = self.copy
      if self.brightness > 0.4
        c.brightness(0.1 + c.brightness*0.25)
      else
        c.brightness(1.0 - c.brightness*0.25)
      end
      colors.push(c)
    
      # A soft supporting color: lighter and less saturated.
      c = self.copy
      c.brightness(0.3 + c.brightness)
      c.saturation(0.1 + c.saturation*0.3)
      colors.push(c)
    
      # A contrasting complement: very dark or very light.
      c_comp = self.copy.complement
      c = c_comp.copy
      if c_comp.brightness > 0.3
        c.brightness(0.1 + c_comp.brightness*0.25)
      else
        c.brightness(1.0 - c.brightness*0.25)
      end
      colors.push(c)    
    
      # The complement
      colors.push(c_comp)
    
      # and a light supporting variant.
      c = c_comp.copy
      c.brightness(0.3 + c.brightness)
      c.saturation(0.1 + c.saturation*0.25)
      colors.push(c)
    
      colors
    end

    # Returns a list with the split complement of the color.
    # The split complement are the two colors to the left and right
    # of the color's complement.
    def split_complementary(angle=30)
        colors = []
        colors.push(self)
        comp = self.copy.complement
        colors.push(comp.copy.rotate_ryb(-angle).lighten(0.1))
        colors.push(comp.copy.rotate_ryb(angle).lighten(0.1))
        colors
    end
  
    # Returns the left half of the split complement.
    # A list is returned with the same darker and softer colors
    # as in the complementary list, but using the hue of the
    # left split complement instead of the complement itself.
    def left_complement(angle=-30)
      left = copy.complement.rotate_ryb(angle).lighten(0.1)
      colors = complementary
      colors[3].hue(left.hue)
      colors[4].hue(left.hue)
      colors[5].hue(left.hue)
      colors
    end
  
    # Returns the right half of the split complement.
    # A list is returned with the same darker and softer colors
    # as in the complementary list, but using the hue of the
    # right split complement instead of the complement itself.
    def right_complement(angle=30)
      right = copy.complement.rotate_ryb(angle).lighten(0.1)
      colors = complementary
      colors[3].hue(right.hue)
      colors[4].hue(right.hue)
      colors[5].hue(right.hue)
      colors
    end
  
    # Returns colors that are next to each other on the wheel.
    # These yield natural color schemes (like shades of water or sky).
    # The angle determines how far the colors are apart, 
    # making it bigger will introduce more variation.
    # The contrast determines the darkness/lightness of
    # the analogue colors in respect to the given colors.
    def analogous(angle=10, contrast=0.25)
      contrast = inrange(contrast, 0.0, 1.0)

      colors = []
      colors.push(self)

      for i, j in [[1,2.2], [2,1], [-1,-0.5], [-2,1]] do
        c = copy.rotate_ryb(angle*i)
        t = 0.44-j*0.1
        if brightness - contrast*j < t then
          c.brightness(t)
        else
          c.brightness(self.brightness - contrast*j)
        end
        c.saturation(c.saturation - 0.05)
        colors.push(c)
      end
      colors
    end

    # Returns colors in the same hue with varying brightness/saturation.  
    def monochrome

        colors = [self]

        c = copy
        c.brightness(_wrap(brightness, 0.5, 0.2, 0.3))
        c.saturation(_wrap(saturation, 0.3, 0.1, 0.3))
        colors.push(c)

        c = copy
        c.brightness(_wrap(brightness, 0.2, 0.2, 0.6))
        colors.push(c)

        c = copy
        c.brightness(max(0.2, brightness+(1-brightness)*0.2))
        c.saturation(_wrap(saturation, 0.3, 0.1, 0.3))
        colors.push(c)

        c = self.copy()
        c.brightness(_wrap(brightness, 0.5, 0.2, 0.3))
        colors.push(c)

        colors
    end
  
    # Returns a triad of colors.
    # The triad is made up of this color and two other colors
    # that together make up an equilateral triangle on 
    # the artistic color wheel.
    def triad(angle=120)
        colors = [self]
        colors.push(copy.rotate_ryb(angle).lighten(0.1))
        colors.push(copy.rotate_ryb(-angle).lighten(0.1))
        colors
    end
  
    # Returns a tetrad of colors.
    # The tetrad is made up of this color and three other colors
    # that together make up a cross on the artistic color wheel.
    def tetrad(angle=90)

        colors = [self]

        c = copy.rotate_ryb(angle)
        if brightness < 0.5 then
          c.brightness(c.brightness + 0.2)
        else
          c.brightness(c.brightness - 0.2)
        end 
        colors.push(c)

        c = copy.rotate_ryb(angle*2)
        if brightness < 0.5
          c.brightness(c.brightness + 0.1)
        else
          c.brightness(c.brightness - 0.1)
        end
      
        colors.push(c)
        colors.push(copy.rotate_ryb(angle*3).lighten(0.1))
        colors
    end
  
    # Roughly the complement and some far analogs.
    def compound(flip=false)
      
        d = (flip ? -1 : 1)
      
        colors = [self]

        c = copy.rotate_ryb(30*d)
        c.brightness(_wrap(brightness, 0.25, 0.6, 0.25))
        colors.push(c)

        c = copy.rotate_ryb(30*d)
        c.saturation(_wrap(saturation, 0.4, 0.1, 0.4))
        c.brightness(_wrap(brightness, 0.4, 0.2, 0.4))
        colors.push(c)

        c = copy.rotate_ryb(160*d)
        c.saturation(_wrap(saturation, 0.25, 0.1, 0.25))
        c.brightness(max(0.2, brightness))
        colors.push(c)

        c = copy.rotate_ryb(150*d)
        c.saturation(_wrap(saturation, 0.1, 0.8, 0.1))
        c.brightness(_wrap(brightness, 0.3, 0.6, 0.3))
        colors.push(c)

        c = copy.rotate_ryb(150*d)
        c.saturation(_wrap(saturation, 0.1, 0.8, 0.1))
        c.brightness(_wrap(brightness, 0.4, 0.2, 0.4))
        #colors.push(c)

        colors
    end
  
    # Roughly the complement and some far analogs.
    def flipped_compound
      compound(true)
    end
  
    private
  
      # vary a single color component by a multiplier
      def vary(original, variance)
        newvalue = original + (rand * variance * (rand > 0.5 ? 1 : -1))
        newvalue = inrange(newvalue,0.0,1.0)
        newvalue
      end
  
      # wrap within range
      def _wrap(x, min, threshold, plus)
        if x - min < threshold
          x + plus
        else
          x - min
        end
      end
  
  end

end
