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
  
  # Make a reusable path. Draw it using canvas.draw(path)
  class Path
  
    attr_accessor :path, :rand, :inc, :fill, :stroke, :scale, :strokewidth, :x, :y, :image
  
    # create a new path, starting at optional x,y
    def initialize(x=0, y=0, &block)
      @path = CGPathCreateMutable()
      @transform = CGAffineTransformMakeTranslation(0,0)
      moveto(x, y)
    
      # set randomized rendering parameters
      @rand = {}
      randomize(:x, 0.0)
      randomize(:y, 0.0)
      randomize(:scale, 1.0)
      randomize(:scalex, 1.0)
      randomize(:scaley, 1.0)
      randomize(:rotation, 0.0)
      randomize(:strokewidth, 1.0)
    
      # set incremental rendering parameters
      @inc = {}
      increment(:rotation, 0.0)
      increment(:x, 0.0)
      increment(:y, 0.0)
      increment(:scale, 1.0)
      increment(:scalex, 1.0)
      increment(:scaley, 1.0)

      @strokewidth = 1.0
      @x = 0.0
      @y = 0.0
      
      if block
        case block.arity
        when 0
          self.send(:instance_eval, &block)
        else
          block.call(self)
        end
      end
      self
    end
  
    def fill(colors=nil)
      if colors
        rand[:fill] = colors
      else
        @fill
      end
    end
  
    # randomize the specified parameter within the specified range
    def randomize(parameter, value)
      rand[parameter] = value
    end
  
    # increment the specified parameter within the specified range
    def increment(parameter, value)
      inc[parameter] = value
    end
  
    # return a mutable clone of this path
    def clone
      newpath = self.dup
      newpath.path = CGPathCreateMutableCopy(@path)
      newpath
    end

  
    # SET PARAMETERS
  
    # set registration mode to :center or :corner
    def registration(mode=:center)
      @registration = mode
    end
  
    # print drawing operations if verbose is true
    def verbose(tf=true)
      @verbose = tf
    end

    # draw without stroke
    def nostroke
      @stroke = nil
    end
  
    # GET PATH INFO
  
    # print origin and dimensions
    def to_s
      "path.to_s: bounding box at [#{originx},#{originy}] with #{width}x#{height}, current point [#{currentpoint[0]},#{currentpoint[1]}]"
    end
  
    # return the x coordinate of the path's origin
    def originx
      CGPathGetBoundingBox(@path).origin.x
    end
  
    # return the y coordinate of the path's origin
    def originy
      CGPathGetBoundingBox(@path).origin.y
    end
  
    # return the width of the path's bounding box
    def width
      CGPathGetBoundingBox(@path).size.width
    end
  
    # return the height of the path's bounding box
    def height
      CGPathGetBoundingBox(@path).size.height
    end
  
    # return the current point
    def currentpoint
      point = CGPathGetCurrentPoint(@path)
      [point.x, point.y]
    end
  
    # true if the path contains the current point # doesn't work?
    def contains(x,y)
      eorule = true
      CGPathContainsPoint(@path, @transform, CGPointMake(x, y), eorule)
    end
  
  
    # ADD SHAPES TO PATH
  
    # add another path to this path
    def addpath(p)
      CGPathAddPath(@path, @transform, p.path)
    end

    # add a rectangle starting at [x,y] with dimensions w x h
    def rect(x=0, y=0, w=20, h=20, reg=@registration)
      if reg == :center
        x = x - w / 2
        y = y - h / 2
      end
      puts "path.rect at [#{x},#{y}] with #{w}x#{h}" if @verbose
      CGPathAddRect(@path, @transform, CGRectMake(x,y,w,h))
      self
    end

    # draw a rounded rectangle using quadratic curved corners (FIXME)
    def roundrect(x=0, y=0, width=20, height=20, roundness=0, reg=@registration)
      if roundness == 0
        p.rect(x, y, width, height, reg)
      else
        if reg == :center
          x = x - self.width / 2
          y = y - self.height / 2
        end
        curve = min(width * roundness, height * roundness)
        p = Path.new
        p.moveto(x, y+curve)
        p.curveto(x, y, x, y, x+curve, y)
        p.lineto(x+width-curve, y)
        p.curveto(x+width, y, x+width, y, x+width, y+curve)
        p.lineto(x+width, y+height-curve)
        p.curveto(x+width, y+height, x+width, y+height, x+width-curve, y+height)
        p.lineto(x+curve, y+height)
        p.curveto(x, y+height, x, y+height, x, y+height-curve)
        p.endpath
      end
      addpath(p)
      self
    end

    # create an oval starting at x,y with dimensions w x h, optionally registered at :center
    def oval(x=0, y=0, w=20, h=20, reg=@registration)
      if (reg == :center)
        x = x - w / 2
        y = y - h / 2
      end
      puts "path.oval at [#{x},#{y}] with #{w}x#{h}" if @verbose
      CGPathAddEllipseInRect(@path, @transform, CGRectMake(x, y, w, h))
      self
    end

    # draw a circle with center at x,y with width and (optional) height
    # def circle(x,y,w,h=w)
    #   oval(x - w/2, y - h/2, w, h)
    # end

    # ADD LINES TO PATH
  
    # draw a line from x1,x2 to x2,y2
    def line(x1, y1, x2, y2)
      CGPathAddLines(@path, @transform, [[x1, y1], [x2, y2]])
      self
    end

    # draw the arc of a circle with center point x,y, radius, start angle (0 deg = 12 o'clock) and end angle
    def arc(x, y, radius, start_angle, end_angle)
      start_angle = radians(90 - start_angle)
      end_angle   = radians(90 - end_angle)
      clockwise   = 1 # 1 = clockwise, 0 = counterclockwise
      CGPathAddArc(@path, @transform, x, y, radius, start_angle, end_angle, clockwise)
      self
    end
  
    # draw lines connecting the array of points
    def lines(points)
      CGPathAddLines(@path, @transform, points)
      self
    end


    # CONSTRUCT PATHS IN PATH OBJECT
  
    # move the "pen" to x,y
    def moveto(x, y)
      CGPathMoveToPoint(@path, @transform,x,y)
      self
    end
  
    # draw a line from the current point to x,y
    def lineto(x,y)
      CGPathAddLineToPoint(@path, @transform, x, y)
      self
    end

    # draw a bezier curve from the current point, given the coordinates of two handle control points and an end point
    def curveto(cp1x, cp1y, cp2x, cp2y, x,  y)
      CGPathAddCurveToPoint(@path, @transform, cp1x, cp1y, cp2x, cp2y, x, y)
      self
    end
  
    # draw a quadratic curve given a single control point and an end point
    def qcurveto(cpx, cpy, x, y)
      CGPathAddQuadCurveToPoint(@path, @transform, cpx, cpy, x, y)
      self
    end
  
    # draw an arc given the endpoints of two tangent lines and a radius
    def arcto(x1, y1, x2, y2, radius)
      CGPathAddArcToPoint(@path, @transform, x1, y1, x2, y2, radius)
      self
    end
  
    # end the current path
    def endpath
      CGPathCloseSubpath(@path)
    end
    

    # TRANSFORMATIONS
  
    # specify rotation for subsequent operations
    def rotate(deg)
      puts "path.rotate #{deg}" if @verbose
      @transform = CGAffineTransformRotate(@transform, radians(deg))
    end
  
    # scale by horizontal/vertical scaling factors sx,sy for subsequent drawing operations
    def scale(sx=nil, sy=nil)
      if sx == nil && sy == nil
        @scale
      else
        sy = sx unless sy
        puts "path.scale #{sx}x#{sy}" if @verbose
        @transform = CGAffineTransformScale(@transform, sx, sy)
      end
    end
  
    # specify translation by x,y for subsequent drawing operations
    def translate(x,y)
      puts "path.translate #{x}x#{y}" if @verbose
      @transform = CGAffineTransformTranslate(@transform, x, y)
    end

  
    # BUILD PRIMITIVES
  
    # draw a petal starting at x,y with w x h and center bulge height using quadratic curves
    def petal(x=0, y=0, w=10, h=50, bulge=h/2)
      moveto(x,y)
      qcurveto(x - w, y + bulge, x, y + h)
      qcurveto(x + w, y + bulge, x, y)
      endpath
      self
    end
  
    # duplicate and rotate the Path object the specified number of times
    def kaleidoscope(path,qty)
      deg = 360 / qty
      qty.times do
        addpath(path)
        rotate(deg)
      end
    end
  
    # duplicate and rotate the Path object the specified number of times
    #path, rotation, scale, translation, iterations
    def spiral(path=nil, rotation=20, scalex=0.95, scaley=0.95, tx=10, ty=10, iterations=30)
      iterations.times do
        addpath(path)
        rotate(rotation)
        scale(scalex, scaley)
        translate(tx, ty)
      end
    end
  

  end
end