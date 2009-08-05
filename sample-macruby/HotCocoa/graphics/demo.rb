require 'hotcocoa'
require 'hotcocoa/graphics'
include HotCocoa
include Graphics
include Math

class DemoView < NSView
  include HotCocoa::Behaviors

  attr_accessor :selected_demo, :adjust

  def self.create
    alloc.initWithFrame(NSZeroRect)
  end

  DEMOS = [
    ['Particles', :draw_particles, true],
    ['Randomize', :draw_randomize, true],
    ['Rope Hair', :draw_rope_hair, false],
    ['Rope Ribbon', :draw_rope_ribbon, false],
    ['Spirograph', :draw_spirograph, true]
  ]

  def self.demo_names
    DEMOS.map { |x| x[0] }
  end

  def initWithFrame(frame)
    if super
      @selected_demo = 0
      @adjust = 0
      self.layout = {}
      return self
    end
  end

  def drawRect(rect)
    canvas = Canvas.for_current_context
    canvas.width = rect.size.width
    canvas.height = rect.size.height
    send(DEMOS[@selected_demo][1], canvas)
  end

  def support_adjust?
    DEMOS[@selected_demo][2]
  end

  private

  def draw_particles(canvas)
    canvas.background(Color.black)
    
    # load images and grab colors
    @redcolors ||= Image.new('images/italy.jpg').saturation(1.9).colors(100)
    @bluecolors ||= Image.new('images/v2.jpg').saturation(1.9).colors(100)
    
    # create flower head shape
    head = Path.new.oval(0,0,10,10,:center)
    petals = 3
    petals.times do
      head.rotate(360/petals)
      head.oval(0,10,5,5,:center)
      head.oval(0,17,2,2,:center)
    end
    # randomize head attributes
    head.randomize(:fill, @redcolors)
    head.randomize(:stroke,@bluecolors)
    head.randomize(:scale,0.2..2.0)
    head.randomize(:rotation,0..360)
    
    # create particles
    numparticles = 100 + @adjust 
    numframes = 100 + @adjust
    particles = []
    numparticles.times do |i|
      # start particle at random point at bottom of canvas
      x = random(canvas.width/2 - 50,canvas.width/2 + 50)
      p = Particle.new(x,0)
      p.velocity_x = random(-0.5,0.5)   # set initial x velocity
      p.velocity_y = random(1.0,3.0)    # set initial y velocity
      p.acceleration = 0.1              # set drag or acceleration
      particles[i] = p                  # add particle to array
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
      color = choose(@bluecolors).a(0.7).analog(20,0.7)
      canvas.stroke(color)
      canvas.strokewidth(random(0.5,2.0))
      # draw the particle
      particles[i].draw(canvas)
      # go to the last particle position and draw the flower head
      canvas.translate(particles[i].points[-1][0],particles[i].points[-1][1])
      canvas.draw(head)
      canvas.pop
    end
  end

  def draw_randomize(canvas)
    canvas.background(Color.white)
    
    # create a flower shape
    shape = Path.new
    petals = 5
    petals.times do
      shape.petal(0, 0, 40, 100)       # petal at x,y with width,height
      shape.rotate(360 / petals)       # rotate by 1/5th
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

    r = 100 + (@adjust * 2)
    r = -r..r
    shape.randomize(:x, r)
    shape.randomize(:y, r)
    
    # draw 50 flowers starting at the center of the canvas
    canvas.translate(canvas.width / 2.0, canvas.height / 2.0)
    canvas.draw(shape, 0, 0, 100 + (@adjust * 2))
  end

  def draw_rope_hair(canvas)
    # choose a random color and set the background to a darker variant
    clr = Color.random.a(0.5)
    canvas.background(clr.copy.darken(0.6))
    
    # create a new rope with 200 fibers
    rope = Rope.new(canvas, :width => 100, :fibers => 200, :strokewidth => 0.4,
        :roundness => 3.0)
    
    # randomly rotate the canvas from its center
    canvas.translate(canvas.width/2, canvas.height/2)
    canvas.rotate(random(0, 360))
    canvas.translate(-canvas.width/2, -canvas.height/2)
    
    # draw 20 ropes
    ropes = 20
    ropes.times do
      # rotate hue up to 20 deg left/right, vary brightness/saturation by up
      # to 70%
      canvas.stroke(clr.copy.analog(20, 0.8))
      rope.x0 = -100                  # start rope off bottom left of canvas
      rope.y0 = -100
      rope.x1 = canvas.width + 100    # end rope off top right of canvas
      rope.y1 = canvas.height + 100
      rope.hair                       # draw rope in organic "hair" style
    end
  end

  def draw_rope_ribbon(canvas)
    # choose a random color and set the background to a darker variant
    clr = Color.random.a(0.5)
    canvas.background(clr.copy.darken(0.6))
    
    # create a new rope with 200 fibers
    rope = Rope.new(canvas, :width => 500, :fibers => 200, :strokewidth => 1.0,
        :roundness => 1.5)
    
    # randomly rotate the canvas from its center
    canvas.translate(canvas.width/2, canvas.height/2)
    canvas.rotate(random(0, 360))
    canvas.translate(-canvas.width/2, -canvas.height/2)
    
    # draw 20 ropes
    ropes = 20
    ropes.times do |i|
       # rotate hue up to 10 deg left/right, vary brightness/saturation by up
       # to 70%
       canvas.stroke(clr.copy.analog(10, 0.7))
       rope.x0 = -100                 # start rope off bottom left of canvas
       rope.y0 = -100
       rope.x1 = canvas.width + 200   # end rope off top right of canvas
       rope.y1 = canvas.height + 200
       rope.ribbon                    # draw rope in smooth "ribbon" style
    end
  end

  def draw_spirograph(canvas)
    canvas.background(Color.beige)
    canvas.fill(Color.black)
    canvas.font('Book Antiqua', 1.0)
    canvas.translate(canvas.width/2, canvas.height/2)
 
    # rotate, draw text, repeat
    180.times do |i|
      canvas.new_state do
        canvas.rotate((i*2) + 120 + (@adjust * 2))
        canvas.translate(70 + (@adjust * 2),0)
        canvas.text('going...', 80, 0)
        canvas.rotate(i * 6)
        canvas.text('Around and', 20, 0)
      end
    end
  end
end

application do

  w = window :title => 'HotCocoa::Graphics Demo!', :size => [480, 540]

  view = DemoView.create
  view.layout = {:expand => [:width, :height]}

  s = slider :min => 0, :max => 100
  s.frameSize = [320, 30]
  s.on_action do |s|
    view.adjust = s.to_i
    view.needsDisplay = true
  end

  box = layout_view :mode => :horizontal, :size => [0, 30], :margin => 0
  box.layout = {:expand => :width}
  box.default_layout = {:align => :center}

  p = popup :items => DemoView.demo_names
  p.on_action do |p|
    view.selected_demo = p.indexOfSelectedItem
    s.intValue = 0
    view.adjust = 0
    s.enabled = view.support_adjust?
    view.needsDisplay = true
  end
  box << p

  b = button :title => 'Refresh'
  b.frameSize = [80, 32]
  b.on_action { view.needsDisplay = true }
  box << b

  b = button :title => 'Quit'
  b.frameSize = [80, 32]
  b.on_action { exit }
  box << b

  w << s
  w << box
  w << view

end
