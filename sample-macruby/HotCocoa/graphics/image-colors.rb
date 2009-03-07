require 'hotcocoa/graphics'
include HotCocoa
include Graphics

OUTFILE = 'image-colors.png'

# set up the canvas
canvas = Canvas.for_image(:size => [400,400], :filename => OUTFILE)
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

canvas.save

`open #{OUTFILE}`
