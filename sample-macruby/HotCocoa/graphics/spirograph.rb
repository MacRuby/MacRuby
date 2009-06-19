#!/usr/bin/env macruby
require 'hotcocoa/graphics'
include HotCocoa
include Graphics

OUTFILE = 'spirograph.png'

# set up the canvas and font
canvas = Canvas.for_image(:size => [400,400], :filename => OUTFILE) do
  background(Color.beige)
  fill(Color.black)
  font('Book Antiqua')
  fontsize(12)
  translate(200,200)

  # rotate, draw text, repeat
  180.times do |frame|
    new_state do
      rotate((frame*2) + 120)
      translate(70,0)
      text('going...', 80, 0)
      rotate(frame * 6)
      text('Around and', 20, 0)
    end
  end
end

# save the canvas
canvas.save

`open #{OUTFILE}`
