#!/usr/bin/env macruby

require 'hotcocoa/graphics'
require 'test/unit'
include HotCocoa::Graphics

include Math

class TestParticle < Test::Unit::TestCase
  
  def test_particle

    # initialize canvas
    canvas = Canvas.for_image(:size => [1920,1200], :filename => 'images/test-particles.png')
    canvas.background(Color.black)

    # load images and grab colors
    img = Image.new('images/italy.jpg').saturation(1.9)
    redcolors = img.colors(100)
    img = Image.new('images/v2.jpg').saturation(1.9)
    bluecolors = img.colors(100)

    # create flower head shape
    head = Path.new.oval(0,0,10,10,:center)
    petals = 3
    petals.times do
      head.rotate(360/petals)
      head.oval(0,10,5,5,:center)
      head.oval(0,17,2,2,:center)
    end
    # randomize head attributes
    head.randomize(:fill,redcolors)
    head.randomize(:stroke,bluecolors)
    head.randomize(:scale,0.2..2.0)
    head.randomize(:rotation,0..360)

    # create particles
    numparticles = 200
    numframes = 200
    particles = []
    numparticles.times do |i|
      # start particle at random point at bottom of canvas
      x = random(canvas.width/2 - 50,canvas.width/2 + 50)
      p = Particle.new(x,0)
      p.velocity_x = random(-0.5,0.5)   # set initial x velocity
      p.velocity_y = random(1.0,3.0)    # set initial y velocity
      p.acceleration = 0.1            # set drag or acceleration
      particles[i] = p          # add particle to array
    end

    # animate particles
    numframes.times do |frame|
      numparticles.times do |i|
        particles[i].move
      end
    end

    # draw particle trails and heads
    numparticles.times do |i|
      canvas.push
      # choose a stem color
      color = choose(bluecolors).a(0.7).analog(20,0.7)
      canvas.stroke(color)
      canvas.strokewidth(random(0.5,2.0))
      # draw the particle
      particles[i].draw(canvas)
      # go to the last particle position and draw the flower head
      canvas.translate(particles[i].points[-1][0],particles[i].points[-1][1])
      canvas.draw(head)
      canvas.pop
    end

    canvas.save   
  end
  
end