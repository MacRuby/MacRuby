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
  
  class Rope
  
    attr_accessor :x0, :y0, :x1, :y1, :width, :fibers, :roundness, :strokewidth
  
    def initialize(canvas, options={})
      @canvas       = canvas
      @width        = options[:width] || 200
      @fibers       = options[:fibers] || 200
      @roundness    = options[:roundness] || 1.0
      @strokewidth  = options[:strokewidth] || 0.4
    end
  
    def hair(hair_x0=@x0, hair_y0=@y0, hair_x1=@x1, hair_y1=@y1, hair_width=@width, hair_fibers=@fibers)
      @canvas.push
      @canvas.strokewidth(@strokewidth)
      @canvas.autoclosepath(false)
      @canvas.nofill
      hair_x0 = choose(hair_x0)
      hair_y0 = choose(hair_y0)
      hair_x1 = choose(hair_x1)
      hair_y1 = choose(hair_y1)
      vx0 = random(-@canvas.width   / 2,  @canvas.width   / 2) * @roundness
      vy0 = random(-@canvas.height  / 2,  @canvas.height  / 2) * @roundness
      vx1 = random(-@canvas.width   / 2,  @canvas.width   / 2) * @roundness
      vy1 = random(-@canvas.height  / 2,  @canvas.height  / 2) * @roundness
      hair_fibers.times do |j|
        #x0,y0,x1,y1 = [@x0.choose,@y0.choose,@x1.choose,@y1.choose]
        @canvas.beginpath(hair_x0, hair_y0)
        @canvas.curveto(
            hair_x0 + vx0 + rand(hair_width), hair_y0 + vy0 + rand(hair_width), # control point 1
            hair_x1 + vx1, hair_y1 + vy1,                                       # control point 2
            hair_x1, hair_y1                                                    # end point
        )
        @canvas.endpath
      end
      @canvas.pop
    end
  
    def ribbon(ribbon_x0=@x0, ribbon_y0=@y0, ribbon_x1=@x1, ribbon_y1=@y1, ribbon_width=@width, ribbon_fibers=@fibers)
      @canvas.push
      @canvas.strokewidth(@strokewidth)
      @canvas.autoclosepath(false)
      @canvas.nofill
      black = Color.black
      white = Color.white
      ribbon_x0 = choose(ribbon_x0)
      ribbon_y0 = choose(ribbon_y0)
      ribbon_x1 = choose(ribbon_x1)
      ribbon_y1 = choose(ribbon_y1)
      vx0 = random(-@canvas.width   / 2, @canvas.width  / 2) * @roundness
      vy0 = random(-@canvas.height  / 2, @canvas.height / 2) * @roundness
      vx1 = random(-@canvas.width   / 2, @canvas.width  / 2) * @roundness
      vy1 = random(-@canvas.height  / 2, @canvas.height / 2) * @roundness
      xwidth = rand(ribbon_width)
      ywidth = rand(ribbon_width)
      ribbon_fibers.times do |j|
        #x0,y0,x1,y1 = [@x0.choose,@y0.choose,@x1.choose,@y1.choose]
        xoffset = (j-1).to_f * xwidth / ribbon_fibers
        yoffset = (j-1).to_f * ywidth / ribbon_fibers
        cpx0 = ribbon_x0 + vx0 + xoffset
        cpy0 = ribbon_y0 + vy0 + yoffset
        cpx1 = ribbon_x1 + vx1
        cpy1 = ribbon_y1 + vy1
        # debug - show control points
        # @canvas.fill(black)
        # @canvas.oval(cpx0,cpy0,5,5,:center)
        # @canvas.fill(white)
        # @canvas.oval(cpx1,cpy1,5,5,:center)
        # @canvas.nofill
      
        @canvas.beginpath(x0, y0)
        @canvas.curveto(
          cpx0, cpy0,           # control point 1
          cpx1, cpy1,           # control point 2
          ribbon_x1, ribbon_y1  # end point
        )
        @canvas.endpath
      end
      @canvas.pop
    end
  end
end