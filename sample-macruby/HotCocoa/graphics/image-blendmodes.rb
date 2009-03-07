require 'hotcocoa/graphics'
include HotCocoa
include Graphics

OUTFILE = 'image-blendmodes.png'

canvas = Canvas.for_image(:size => [400,730], :filename => OUTFILE)
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

`open #{OUTFILE}`
