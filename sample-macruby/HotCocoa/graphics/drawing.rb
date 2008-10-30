#!/usr/bin/env macruby

require 'hotcocoa/graphics'
require 'test/unit'
include HotCocoa::Graphics

class TestDrawing < Test::Unit::TestCase
  
  def test_save_png
    c = Canvas.for_image(:size => [400,400], :filename =>'images/test-png.png') do
      background(Color.white)
      fill(Color.black)
      text("Hello PNG!",150,200)
    end
    assert(true, c.save)
    #c.open
  end
  
  def test_save_pdf
    c = Canvas.for_image(:size => [400,400], :filename =>'images/test-pdf.pdf')
    c.background(Color.white)
    c.fill(Color.black)
    c.text("Hello PDF!",150,200)
    assert(true, c.save)
   # c.open
  end
  
  def test_drawing
    # specify canvas dimensions and output file (pdf, png, tif, gif)
    File.delete('images/test-drawing.png') if File.exists?('images/test-drawing.png')
    c = Canvas.for_image(:size => [400,400], :filename =>'images/test-drawing.png') do
      # set the background fill color
      background(Color.white)
      # draw a reference grid
      cartesian

      # SET CANVAS OPTIONS
      #translate(200,200)     # set initial drawing point
      #antialias(false)       # turn off antialiasing for some crazy reason
      #alpha(0.5)             # set transparency for drawing operations
      #fill(Color.blue.dark)  # set the fill color
      #nofill                 # disable fill

      # SET CANVAS LINE STYLE
      nofill
      stroke(Color.black)
      strokewidth(5)
      linecap(:round)         # render line endings as :round, :butt, or :square 
      linejoin(:round)        # render line joins as :round, :bevel (flat), or :miter (pointy)

      # DRAW LINES ON CANVAS
      arc(200,200,50,-45,180)           # an arc of a circle with center x,y with radius, start angle, end angle
      line(0,0,200,200)                 # a line with start X, start Y, end X, end Y
      lines([[400,400],[300,400],[200,300],[400,300],[400,400]])  # array of points
      qcurve(200,200,0,0,400,0)         # a quadratic bezier curve with control point cpx,cpy and endpoints x1,y1,x2,y2
      curve(200,200,200,400,0,0,0,400)  # a bezier curve with control points cp1x,cp1y,cp2x,cp2y and endpoints x1,y1,x2,y2

      # CONSTRUCT COMPLEX PATHS DIRECTLY ON CANVAS
      fill(Color.red)                   # set the fill color for the new path
      autoclosepath(true)               # automatically close the path when calling `endpath`
      new_path 50, 300 do
        curveto(150,250,150,200,50,200)
        lineto(0,250)
        #moveto(25,250)
        qcurveto(25,300,50,250)
        arcto(50,225,100,225,20)
      end

      # CONSTRUCT A REUSABLE PATH OBJECT
      p = Path.new
      p.scale(0.75)
      p.translate(350,0)
      p.moveto(50,300)
      p.curveto(150,250,150,200,50,200)
      p.lineto(0,250)
      p.qcurveto(25,300,50,250)
      #p.moveto(25,250)
      p.arcto(50,225,100,225,20)
      p.endpath
      p.contains(100,250)   # doesn't work?
      strokewidth(1)
      fill(Color.orange.darken)
      draw(p)     # draw it to the canvas

      # CONSTRUCT A PATH WITH SHAPES
      p2 = Path.new
      p2.rect(0,0,50,50)
      p2.oval(-50,-50,50,50)
      fill(Color.blue)
      draw(p2,50,100)           # draw the path at x,y (or current point)
      draw(p2,150,100)          # draw the path at x,y (or current point)

      # GET PATH INFO
      puts "path bounding box at [#{p.x},#{p.y}] with dimensions #{p.width}x#{p.height}, current point is #{p.currentpoint}"

      # DRAW TEXT
      # save the previous drawing state (font, colors, position, etc)
      new_state do
        font('Times-Bold')
        fontsize(20)
        translate(150,350)
        text("bzzzzt",0,10)
        # restore the previous drawing state
      end

      # DRAW GRADIENTS, CLIPPING PATHS
      new_state do                        # save the previous state
        translate(300,300)          # move to a new drawing location

        # create gradient
        g = Gradient.new([Color.yellow,Color.violet.darken.a(0.5),Color.orange])  # create a gradient

        # linear:
        m = Path.new                  # create a new path to use for clipping
        m.oval(-50,-50,100,100)       # draw a circle in the path
        beginclip(m) do                # tell the canvas to use the clipping path
          gradient(g,0,-50,0,50)      # draw a linear gradient between the two points
        end                           # tell the canvas to stop using the clipping path

        # radial:
        translate(0,-200)           # move down a bit
        beginclip(m) do              # begin clipping again
          radial(g,0,0,50)          # draw radial gradient starting at x,y using radius
        end                         # end clipping
      end                           # restore the previous state
    end
    

    # SAVE THE CANVAS TO OUTPUT FILE
    assert(true,c.save)
    #assert_equal('', `diff images/test-drawing.png images/fixture-drawing.png`)
    #c.open
  end
  
end