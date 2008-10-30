#!/usr/bin/env macruby

require 'hotcocoa/graphics'
require 'test/unit'
include HotCocoa::Graphics


class TestIterate < Test::Unit::TestCase
  
  def test_iterate

    # create a new 400×400 pixel canvas to draw on
    canvas = Canvas.for_image(:size => [400,400], :filename => 'images/test-iterating.png') do
      background(Color.white)

      # create a petal shape with base at (0,0), size 40×150, and bulge at 30px
      shape = Path.new
      shape.petal(0,0,40,150,30)
      # add a circle
      shape.oval(-10,20,20,20)
      # color it red
      shape.fill(Color.red)

      # increment shape parameters by the specified amount each iteration,
      # or by a random value selected from the specified range
      shape.increment(:rotation, 5.0)
      #shape.increment(:scale, 0.95)
      shape.increment(:scalex, 0.99)
      shape.increment(:scaley, 0.96)
      shape.increment(:x, 10.0)
      shape.increment(:y, 12.0)
      shape.increment(:hue,-0.02..0.02)
      shape.increment(:saturation, -0.1..0.1)
      shape.increment(:brightness, -0.1..0.1)
      shape.increment(:alpha, -0.1..0.1)

      # draw 200 petals on the canvas starting at location 50,200
      translate(50,220)
      draw(shape,0,0,200)
      save
    end
    
  end
  
end