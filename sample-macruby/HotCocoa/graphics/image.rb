#!/usr/bin/env macruby

require 'hotcocoa/graphics'
require 'test/unit'
include HotCocoa::Graphics


#PRECISION = 0.000000000000001
PRECISION = 0.0000001

class TestImage < Test::Unit::TestCase
  
  def test_image_moving
    
    # setup the canvas
    File.delete('images/test-image-moving.png') if File.exists?('images/test-image-moving.png')
    canvas = Canvas.for_image(:size => [400,400], :filename => 'images/test-image-moving.png')
    canvas.background(Color.white)
    canvas.font('Skia')
    canvas.fontsize(14)
    canvas.fill(Color.black)
    canvas.stroke(Color.red)

    # load an image
    img = Image.new('images/v2.jpg')

    # SCALE (multiply both dimensions by a scaling factor)
    img.scale(0.2)
    canvas.draw(img,0,240)  # draw the image at the specified coordinates
    canvas.text("scale to 20%",0,220)

    # FIT (scale to fit within the given dimensions, maintaining original aspect ratio)
    img.reset               # first reset the image to its original size
    img.fit(100,100)
    canvas.fill(Color.white)
    canvas.rect(120,240,100,100)
    canvas.fill(Color.black)
    canvas.draw(img,133,240)
    canvas.text("fit into 100x100",120,220)

    # RESIZE (scale to fit exactly within the given dimensions)
    img.reset
    img.resize(100,100)
    canvas.draw(img,240,240)
    canvas.text("resize to 100x100",240,220)

    # CROP (to the largest square containing image data)
    img.reset
    img.scale(0.2).crop
    canvas.draw(img,0,100)
    canvas.text("crop max square",0,80)

    # CROP (within a rectangle starting at x,y with width,height)
    img.reset
    img.scale(0.3).crop(0,0,100,100)
    canvas.draw(img,120,100)
    canvas.text("crop to 100x100",120,80)

    # ROTATE
    img.origin(:center)
    img.rotate(45)           
    canvas.draw(img,300,140)
    canvas.text("rotate 45 degrees",250,50)

    #img.origin(:center)   # center the image
    #img.translate(0,-150)    # move the image

    assert(true, canvas.save)
    assert_equal('', `diff images/test-image-moving.png images/fixture-image-moving.png`)
    #canvas.open
  end
  
  def test_image_effects
    # set up the canvas
    File.delete('images/test-image-effects.png') if File.exists?('images/test-image-effects.png')
    canvas = Canvas.for_image(:size => [400,400], :filename => 'images/test-image-effects.png')
    canvas.background(Color.white)
    canvas.font('Skia')
    canvas.fontsize(14)
    canvas.fill(Color.black)

    # load image file
    img = Image.new('images/v2.jpg')

    # set image width/height, starting position, and increment position
    w,h = [100,100]
    x,y = [0,290]
    xoffset = 105
    yoffset = 130

    # ORIGINAL image, resized to fit within w,h:
    img.fit(w,h)
    canvas.draw(img,x,y)
    canvas.text("original",x,y-15)
    x += xoffset

    # CRYSTALLIZE: apply a "crystallize" effect with the given radius
    img.reset.fit(w,h)
    img.crystallize(5.0)
    canvas.draw(img,x,y)
    canvas.text("crystallize",x,y-15)
    x += xoffset

    # BLOOM: apply a "bloom" effect with the given radius and intensity
    img.reset.fit(w,h)
    img.bloom(10, 1.0)
    canvas.draw(img,x,y)
    canvas.text("bloom",x,y-15)
    x += xoffset

    # EDGES: detect edges
    img.reset.fit(w,h)
    img.edges(10)
    canvas.draw(img,x,y)
    canvas.text("edges",x,y-15)
    x += xoffset

    # (go to next row)
    x = 0
    y -= yoffset

    # POSTERIZE: reduce image to the specified number of colors
    img.reset.fit(w,h)
    img.posterize(8)
    canvas.draw(img,x,y)
    canvas.text("posterize",x,y-15)
    x += xoffset

    # TWIRL: rotate around x,y with radius and angle
    img.reset.fit(w,h)
    img.twirl(35,50,40,90)
    canvas.draw(img,x,y)
    canvas.text("twirl",x,y-15)
    x += xoffset

    # EDGEWORK: simulate a woodcut print
    img.reset.fit(w,h)
    canvas.rect(x,y,img.width,img.height) # needs a black background
    img.edgework(0.5)
    canvas.draw(img,x,y)
    canvas.text("edgework",x,y-15)
    x += xoffset

    # DISPLACEMENT: use a second image as a displacement map
    img.reset.fit(w,h)
    img2 = Image.new('images/italy.jpg').resize(img.width,img.height)
    img.displacement(img2, 30.0)
    canvas.draw(img,x,y)
    canvas.text("displacement",x,y-15)
    x += xoffset

    # (go to next row)
    x = 0
    y -= yoffset

    # DOTSCREEN: simulate a dot screen: center point, angle(0-360), width(1-50), and sharpness(0-1)
    img.reset.fit(w,h)
    img.dotscreen(0,0,45,5,0.7)
    canvas.draw(img,x,y)
    canvas.text("dotscreen",x,y-15)
    x += xoffset

    # SHARPEN: sharpen using the given radius and intensity
    img.reset.fit(w,h)
    img.sharpen(2.0,2.0)
    canvas.draw(img,x,y)
    canvas.text("sharpen",x,y-15)
    x += xoffset

    # BLUR: apply a gaussian blur with the given radius
    img.reset.fit(w,h)
    img.blur(3.0)
    canvas.draw(img,x,y)
    canvas.text("blur",x,y-15)
    x += xoffset

    # MOTION BLUR: apply a motion blur with the given radius and angle
    img.reset.fit(w,h)
    img.motionblur(10.0,90)
    canvas.draw(img,x,y)
    canvas.text("motion blur",x,y-15)
    x += xoffset

    # save the canvas
    assert(true, canvas.save)
    assert_equal('', `diff images/test-image-effects.png images/fixture-image-effects.png`)
    #canvas.open
  end
  
  def test_image_colors
    
    # set up the canvas
    File.delete('images/test-image-colors.png') if File.exists?('images/test-image-colors.png')
    canvas = Canvas.for_image(:size => [400,400], :filename => 'images/test-image-colors.png')
    canvas.background(Color.white)
    canvas.font('Skia')
    canvas.fontsize(14)
    canvas.fill(Color.black)

    # LOAD IMAGE
    img = Image.new('images/v2.jpg')

    w,h = [100,100]
    x,y = [0,290]
    xoffset = 105
    yoffset = 130

    # ORIGINAL image, resized to fit within w,h:
    img.fit(w,h)
    canvas.draw(img,x,y)
    canvas.text("original",x,y-15)
    x += xoffset

    # HUE: rotate color wheel by degrees
    img.reset.fit(w,h)
    img.hue(45)
    canvas.draw(img,x,y)
    canvas.text("hue",x,y-15)
    x += xoffset
    
    # EXPOSURE: increase/decrease exposure by f-stops
    img.reset.fit(w,h)
    img.exposure(1.0)
    canvas.draw(img,x,y)
    canvas.text("exposure",x,y-15)
    x += xoffset
    
    # SATURATION: adjust saturation by multiplier
    img.reset.fit(w,h)
    img.saturation(2.0)
    canvas.draw(img,x,y)
    canvas.text("saturation",x,y-15)
    x += xoffset
    
    # (go to next row)
    x = 0
    y -= yoffset
    
    # CONTRAST: adjust contrast by multiplier
    img.reset.fit(w,h)
    img.contrast(2.0)
    canvas.draw(img,x,y)
    canvas.text("contrast",x,y-15)
    x += xoffset
    
    # BRIGHTNESS: adjust brightness
    img.reset.fit(w,h)
    img.brightness(0.5)
    canvas.draw(img,x,y)
    canvas.text("brightness",x,y-15)
    x += xoffset
    
    # MONOCHROME: convert to a monochrome image
    img.reset.fit(w,h)
    img.monochrome(Color.orange)
    canvas.draw(img,x,y)
    canvas.text("monochrome",x,y-15)
    x += xoffset
    
    # WHITEPOINT: remap the white point
    img.reset.fit(w,h)
    img.whitepoint(Color.whiteish)
    canvas.draw(img,x,y)
    canvas.text("white point",x,y-15)
    x += xoffset
    
    # (go to next row)
    x = 0
    y -= yoffset
    
    # CHAINING: apply multiple effects at once
    img.reset.fit(w,h)
    img.hue(60).saturation(2.0).contrast(2.5)
    canvas.draw(img,x,y)
    canvas.text("multi effects",x,y-15)
    x += xoffset
    
    # COLORS: sample random colors from the image and render as a gradient
    img.reset.fit(w,h)              # reset the image and scale to fit within w,h
    colors = img.colors(10).sort!   # select 10 random colors and sort by brightness
    # gradient
    gradient = Gradient.new(colors) # create a new gradient using the selected colors
    rect = Path.new.rect(x,y,img.width,img.height) # create a rectangle the size of the image
    canvas.beginclip(rect)          # begin clipping so the gradient will only fill the rectangle
    canvas.gradient(gradient,x,y,x+img.width,y+img.height) # draw the gradient between opposite corners of the rectangle
    canvas.endclip                  # end clipping so we can draw on the rest of the canvas
    canvas.text("get colors",x,y-15)    # add text label
    x += xoffset

    assert(true, canvas.save)
    # hmm, can't use an image fixture if we're doing randomized things
    # assert_equal('', `diff images/test-image-colors.png images/fixture-image-colors.png`)
  end
  
  def test_image_blendmodes
    
    canvas = Canvas.for_image(:size => [400,730], :filename => 'images/test-image-blendmodes.png')
    canvas.background(Color.white)
    canvas.font('Skia')
    canvas.fontsize(14)

    # set image width,height
    w,h = [95,95]

    # set initial drawing position
    x,y = [0,canvas.height - h - 10]

    # load and resize two images
    imgA = Image.new('images/v2.jpg').resize(w,h)
    imgB = Image.new('images/italy.jpg').resize(w,h)

    # add image B to image A using each blending mode, and draw to canvas
    [:normal,:multiply,:screen,:overlay,:darken,:lighten,
      :colordodge,:colorburn,:softlight,:hardlight,:difference,:exclusion,
      :hue,:saturation,:color,:luminosity,:maximum,:minimum,:add,:atop,
      :in,:out,:over].each do |blendmode|
      imgA.reset.resize(w,h)
      imgA.blend(imgB, blendmode)
      canvas.draw(imgA,x,y)
      canvas.text(blendmode,x,y-15)
      x += w + 5
      if (x > canvas.width - w + 5)
        x = 0
        y -= h + 25
      end
    end
    canvas.save
    
  end
  
  
end