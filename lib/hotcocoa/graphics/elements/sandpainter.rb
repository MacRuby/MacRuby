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

  # draw watercolor-like painted strokes (adapted from code by Jared Tarbell - complexification.net)
  class SandPainter

    attr_accessor :color, :grains, :grainsize, :maxalpha, :jitter, :huedrift, :saturationdrift, :brightnessdrift

    def initialize(canvas, color=Color.red)
      @canvas           = canvas
      @color            = color
      # set a random initial gain value
      @gain             = random(0.01, 0.1)
      @grainsize        = 6.0
      @grains           = 64
      @maxalpha         = 0.1
      @jitter           = 0
      @huedrift         = 0.2
      @saturationdrift  = 0.3
      @brightnessdrift  = 0.3
    end
  
    # render a line that fades out from ox,oy to x,y
    def render(ox, oy, x, y) 
      @canvas.push
      # modulate gain
      @gain += random(-0.050, 0.050)
      @gain = inrange(@gain, 0.0, 1.0)
      # calculate grains by distance
      #@grains = (sqrt((ox-x)*(ox-x)+(oy-y)*(oy-y))).to_i

      # ramp from 0 to .015 for g = 0 to 1
      w = @gain / (@grains-1)
    
      #raycolor = @color.analog
      #@color.drift(0.2,0.1,0.1)
      @color.drift(huedrift, saturationdrift, brightnessdrift)
      #for i in 0..@grains do ##RACK change to fixnum.times
      (@grains + 1).times do |i|
        # set alpha for this grain (ramp from maxalpha to 0.0 for i = 0 to 64)
        a = @maxalpha - (i / @grains.to_f) * @maxalpha
        fillcolor = @color.copy.a(a)
        @canvas.fill(fillcolor)
        #C.rect(ox+(x-ox)*sin(sin(i*w)),oy+(y-oy)*sin(sin(i*w)),1,1)
        scaler = sin(sin(i * w)) # ramp sinusoidally from 0 to 1
        x1 = ox + (x - ox) * scaler + random(-@jitter, @jitter)
        y1 = oy + (y - oy) * scaler + random(-@jitter, @jitter)
        #puts "#{scaler} #{w} #{i} #{a} => #{x1},#{y1} #{scaler}"
        @canvas.oval(x1, y1, @grainsize, @grainsize, :center)
        #C.oval(x,y,@grainsize,@grainsize,:center)
        #C.oval(ox,oy,@grainsize,@grainsize,:center)
      end
      @canvas.pop
    
    end
  end
end