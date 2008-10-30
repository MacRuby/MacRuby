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

module HotCocoa;end # needed in case this is required without hotcocoa

framework 'Cocoa'

module HotCocoa::Graphics

  # UTILITY FUNCTIONS (math/geometry)
  TEST = 'OK'

  # convert degrees to radians
  def radians(deg)
    deg * (Math::PI / 180.0)
  end

  # convert radians to degrees
  def degrees(rad)
    rad * (180 / Math::PI)
  end

  # return the angle of the line joining the two points
  def angle(x0, y0, x1, y1)
    degrees(Math.atan2(y1-y0, x1-x0))
  end

  # return the distance between two points
  def distance(x0, y0, x1, y1)
    Math.sqrt((x1-x0)**2 + (y1-y0)**2)
  end

  # return the coordinates of a new point at the given distance and angle from a starting point
  def coordinates(x0, y0, distance, angle)
    x1 = x0 + Math.cos(radians(angle)) * distance
    y1 = y0 + Math.sin(radians(angle)) * distance
    [x1,y1]
  end

  # return the lesser of a,b
  def min(a, b)
    a < b ? a : b
  end

  # return the greater of a,b
  def max(a, b)
    a > b ? a : b
  end

  # restrict the value to stay within the range
  def inrange(value, min, max)
    if value < min
      min
    elsif value > max
      max
    else
      value
    end
  end

  # return a random number within the range, or a float from 0 to the number
  def random(left=nil, right=nil)
    if right
      rand * (right - left) + left
    elsif left
      rand * left
    else
      rand
    end
  end

  def reflect(x0, y0, x1, y1, d=1.0, a=180)
    d *= distance(x0, y0, x1, y1)
    a += angle(x0, y0, x1, y1)
    x, y = coordinates(x0, y0, d, a)
    [x,y]
  end

  def choose(object)
    case object
    when Range
      case object.first
      when Float
        rand * (object.last - object.first) + object.first
      when Integer
        rand(object.last - object.first + 1) + object.first
      end
    when Array
      object.choice
    else
      object
    end
  end

  # given an object's x,y coordinates and dimensions, return the distance 
  # needed to move in order to orient the object at the given location (:center, :bottomleft, etc)
  def reorient(x, y, w, h, location)
    case location
    when :bottomleft
      movex = -x
      movey = -y
    when :centerleft
      movex = -x
      movey = -y - h / 2
    when :topleft
      movex = -x
      movey = -x - h
    when :bottomright
      movex = -x - w
      movey = -y
    when :centerright
      movex = -x - w
      movey = -y - h / 2
    when :topright
      movex = -x - w
      movey = -y - h
    when :bottomcenter
      movex = -x - w / 2
      movey = -y
    when :center
      movex = -x - w / 2
      movey = -y - h / 2
    when :topcenter
      movex = -x - w / 2
      movey = -y - h
    else
      raise "ERROR: image origin locator not recognized: #{location}"
    end
    #newx = oldx + movex
    #newy = oldy + movey
    [movex,movey]
  end

end

require 'hotcocoa/graphics/canvas'
require 'hotcocoa/graphics/color'
require 'hotcocoa/graphics/gradient'
require 'hotcocoa/graphics/image'
require 'hotcocoa/graphics/path'
require 'hotcocoa/graphics/pdf'
require 'hotcocoa/graphics/elements/particle'
require 'hotcocoa/graphics/elements/rope'
require 'hotcocoa/graphics/elements/sandpainter'
