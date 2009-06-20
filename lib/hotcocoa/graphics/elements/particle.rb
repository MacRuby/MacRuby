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
  
  # wandering particle with brownian motion
  class Particle 

    attr_accessor :acceleration, :points, :stroke, :velocity_x, :velocity_y, :x, :y

    # initialize particle origin x,y coordinates (relative to the center)
    def initialize (x, y, velocity_x=0.0, velocity_y=2.0)
      @age = 0
      @acceleration = 0.5
    
      @x = x
      @y = y
    
      @previous_x = 0
      @previous_y = 0

      # initialize velocity
      @velocity_x=velocity_x
      @velocity_y=velocity_y
    
      # append the point to the array
      @points = [NSPoint.new(@x, @y)]
      @stroke = Color.white
    end
  
    # move to a new position using brownian motion
    def move
      # save old x,y position
      @previous_x=@x
      @previous_y=@y

      # move particle by velocity_x,velocity_y
      @x += @velocity_x
      @y += @velocity_y

      # randomly increase/decrease direction
      @velocity_x += random(-1.0, 1.0) * @acceleration
      @velocity_y += random(-1.0, 1.0) * @acceleration

      # draw a line from the old position to the new
      #CANVAS.line(@previous_x,@previous_y,@x,@y);
      @points.push(NSPoint.new(@x, @y))
    
      # grow old
      @age += 1
      if @age>200
        # die and be reborn
      end
    
    end
  
    def draw(canvas)
      canvas.nofill
      canvas.lines(@points)
    end
  
  end
end
