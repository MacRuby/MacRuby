#!/usr/bin/env macruby

require 'hotcocoa/graphics'
require 'test/unit'
include HotCocoa::Graphics


PRECISION = 0.0000001

class TestHSB < Test::Unit::TestCase
  
  def test_hsba
    
    # new named color
    c = Color.blue
    assert_in_delta(c.hue, 0.6666666, PRECISION)
    assert_in_delta(c.saturation,1.0, PRECISION)
    assert_in_delta(c.brightness,1.0, PRECISION)
    assert_in_delta(c.a,1.0, PRECISION)
    
    # new HSBA color
    c = Color.hsb(0.5,0.4,0.3,0.2)
    assert_in_delta(c.hue, 0.5, PRECISION)
    assert_in_delta(c.saturation, 0.4, PRECISION)
    assert_in_delta(c.brightness, 0.3, PRECISION)
    assert_in_delta(c.a, 0.2, PRECISION)
    
    # new RGBA color
    c = Color.new(0.5,0.4,0.3,1.0)
    assert_in_delta(c.hue, 0.08333333, PRECISION)
    assert_in_delta(c.saturation, 0.3999999, PRECISION)
    assert_in_delta(c.brightness, 0.5, PRECISION)
    assert_in_delta(c.a, 1.0, PRECISION)
    
    # darken
    c = Color.blue
    c.darken(0.1)
    assert_in_delta(c.hue, 0.6666666, PRECISION)
    assert_in_delta(c.saturation,1.0, PRECISION)
    assert_in_delta(c.brightness, 0.8999999, PRECISION)
    assert_in_delta(c.a,1.0, PRECISION)
    
    # lighten
    c = Color.new(0.5,0.4,0.3,1.0)
    c.lighten(0.1)
    assert_in_delta(c.hue, 0.08333333, PRECISION)
    assert_in_delta(c.saturation, 0.4, PRECISION)
    assert_in_delta(c.brightness, 0.6, PRECISION)
    assert_in_delta(c.a, 1.0, PRECISION)
  
    # saturate
    c = Color.new(0.5,0.4,0.3,1.0)
    c.saturate(0.1)
    assert_in_delta(c.hue, 0.08333333, PRECISION)
    assert_in_delta(c.saturation, 0.5, PRECISION)
    assert_in_delta(c.brightness, 0.5, PRECISION)
    assert_in_delta(c.a, 1.0, PRECISION)
    
    # desaturate
    c = Color.new(0.5,0.4,0.3,1.0)
    c.desaturate(0.1)
    assert_in_delta(c.hue, 0.08333333, PRECISION)
    assert_in_delta(c.saturation, 0.2999999, PRECISION)
    assert_in_delta(c.brightness, 0.5, PRECISION)
    assert_in_delta(c.a, 1.0, PRECISION)
    
    # set HSB
    c.set_hsb(0.5,0.4,0.3,0.2)
    assert_in_delta(c.hue, 0.5, PRECISION)
    assert_in_delta(c.saturation, 0.4, PRECISION)
    assert_in_delta(c.brightness, 0.3, PRECISION)
    assert_in_delta(c.a, 0.2, PRECISION)
    
    # get HSB
    h,s,b,a = c.get_hsb
    assert_in_delta(h, 0.5, PRECISION)
    assert_in_delta(s, 0.4, PRECISION)
    assert_in_delta(b, 0.3, PRECISION)
    assert_in_delta(a, 0.2, PRECISION)
    
    # adjust HSB
    c.adjust_hsb(0.1,0.1,0.1,0.1)
    assert_in_delta(c.hue, 0.6, PRECISION)
    assert_in_delta(c.saturation, 0.5, PRECISION)
    assert_in_delta(c.brightness, 0.4, PRECISION)
    assert_in_delta(c.a, 0.3, PRECISION)
    
  end
  
  def test_rgba
    
    # new named color
    c = Color.blue
    assert_in_delta(c.r, 0.0, PRECISION)
    assert_in_delta(c.g, 0.0, PRECISION)
    assert_in_delta(c.b, 1.0, PRECISION)
    assert_in_delta(c.a, 1.0, PRECISION)
    
    # new RGBA color
    c = Color.new(0.5,0.4,0.3,0.2)
    assert_in_delta(c.r, 0.5, PRECISION)
    assert_in_delta(c.g, 0.4, PRECISION)
    assert_in_delta(c.b, 0.3, PRECISION)
    assert_in_delta(c.a, 0.2, PRECISION)

    # new RGBA color
    c = Color.rgb(0.5,0.4,0.3,0.2)
    assert_in_delta(c.r, 0.5, PRECISION)
    assert_in_delta(c.g, 0.4, PRECISION)
    assert_in_delta(c.b, 0.3, PRECISION)
    assert_in_delta(c.a, 0.2, PRECISION)

    # adjust RGBA
    c.adjust_rgb(0.1,0.1,0.1,0.1)
    assert_in_delta(c.r, 0.6, PRECISION)
    assert_in_delta(c.g, 0.5, PRECISION)
    assert_in_delta(c.b, 0.4, PRECISION)
    assert_in_delta(c.a, 0.3, PRECISION)

    
  end
  
  
end