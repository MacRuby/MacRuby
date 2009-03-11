#!/usr/bin/env macruby

require 'hotcocoa/graphics'
require 'test/unit'
include HotCocoa::Graphics


class TestRandomize < Test::Unit::TestCase
  
  def test_randomize

    # create a new 400×400 pixel canvas to draw on
    canvas = Canvas.for_image(:size => [400,400], :filename => 'images/test-randomize.png')
    canvas.background(Color.white)

    # create a flower shape
    shape = Path.new
    petals = 5
    for i in 1..petals do
      shape.petal(0,0,40,100)       # petal at x,y with width,height
      shape.rotate(360 / petals)    # rotate by 1/5th
    end

    # randomize shape parameters
    shape.randomize(:fill, Color.blue.complementary)
    shape.randomize(:stroke, Color.blue.complementary)
    shape.randomize(:strokewidth, 1.0..10.0)
    shape.randomize(:rotation, 0..360)
    shape.randomize(:scale, 0.5..1.0)
    shape.randomize(:scalex, 0.5..1.0)
    shape.randomize(:scaley, 0.5..1.0)
    shape.randomize(:alpha, 0.5..1.0)
    # shape.randomize(:hue, 0.5..0.8)
    shape.randomize(:saturation, 0.0..1.0)
    shape.randomize(:brightness, 0.0..1.0)
    shape.randomize(:x, -100.0..100.0)
    shape.randomize(:y, -100.0..100.0)

    # draw 50 flowers starting at the center of the canvas
    canvas.translate(200,200)
    canvas.draw(shape,0,0,100)
    canvas.save
    
  end
  
end