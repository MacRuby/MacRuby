require 'hotcocoa/graphics'
include HotCocoa
include Graphics

PRECISION = 0.0000001

OUTFILE = 'image-moving.png'

# setup the canvas
canvas = Canvas.for_image(:size => [400,400], :filename => OUTFILE)
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

canvas.save

`open #{OUTFILE}`
