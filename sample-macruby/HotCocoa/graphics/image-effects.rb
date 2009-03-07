require 'hotcocoa/graphics'
include HotCocoa
include Graphics

OUTFILE = 'image-effects.png'

# set up the canvas
canvas = Canvas.for_image(:size => [400,400], :filename => OUTFILE)
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
canvas.save

`open #{OUTFILE}`
