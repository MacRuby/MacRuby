#  image_layer.rb
#  Phileas Frog
#
#  Copyright 2009 Matt Aimonetti
# 
#  Full version of the game available at http://github.com/mattetti/phileas_frog/
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
# 
#      http://www.apache.org/licenses/LICENSE-2.0
# 
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#

# ImageLayer is probably one of the most important class of the game
# Because I decided to use CoreAnimation instead of OpenGL
# each element needs to live on a Core Animation Layer
# 
# Layers can be created with an image that's being passed
# (see initiWithImageNamed(file_name)
# or by passing a game item.
#
# Because we are dealing with a Cocoa subclass
# we need to allocate the object the same way we would do
# in Objective-C
# for instance:
# ImageLayer.alloc.initWithImageNamed('disco')
#
# We define a delegate method called drawInContext(ctx)
# that is being dispatched when the layer needs to be redrawn.

class ImageLayer < CALayer
  attr_reader :item
  attr_accessor :x, :y, :width, :height

  # instead of overloading init and calling super
  # we need to follow the Cocoa conventions and setting up our own
  # initWith method.
  # Also, let's make sure we return self 
  def initWithImageNamed(file_name)
    init
    backgroundColor = CGColorCreateGenericRGB(0, 10, 0, 1)
    anchorPoint = CGPointMake(0.5, 0.5)
    @image_name = file_name
    refresh

    setFrame(CGRect.new)
    # don't forget to return self 
    self
  end

  def initWithItem(item)
    init
    @item = item
    refresh
    update    
    self
  end

  def image_name
    item ? item.image_name : @image_name
  end
  
  # swap a layer image
  # if no images are passed, the layer gets hidden
  # practical when changing the background for instance
  #
  def change_image(image_name=nil, width=nil, height=nil)
    
    if item
      if image_name.nil? && @item
        @item.visible = false 
      else        
        @item.image_name = image_name
        @item.visible = true
      end
    else
      @image_name = image_name
    end
    
    @item.width = width if @item && width
    @item.height = height if @item && height
    refresh
  end

  # If a layer is updated
  # we are looking at its game item and
  # repositioning the layer based on the item settings
  def update
    item.update if item.respond_to?(:update)
    game_height = GameData.game_height
    game_width  = GameData.game_height

    @x       = item.x * game_width
    @y       = item.y * game_height
    @width   = item.width * game_width
    @height  = item.height * game_height
    angle    = item.angle
    visible  = item.visible

    self.bounds    = CGRectMake(0, 0, @width, @height)
    self.position  = CGPointMake(@x, @y)
    self.transform = CATransform3DMakeRotation(angle, 0, 0, 1.0)
    self.hidden    = !visible 
  end

  # for debugging purposes
  def to_s
    "image: #{image_name}, position: #{position.to_a}"
  end 

  # simple alias that seems to make more sense
  def refresh
    setNeedsDisplay
  end

  # drawing of the layer
  # never called directly
  def drawInContext(ctx)
    return unless image_name
    old_context = NSGraphicsContext.currentContext
    context = NSGraphicsContext.graphicsContextWithGraphicsPort(ctx, flipped:false)
    NSGraphicsContext.currentContext = context
    image = NSImage.imageNamed(image_name) 
    raise "Image missing, can't draw the item #{self}" if image == nil
    image.drawInRect(NSRectFromCGRect(bounds), fromRect: image.alignmentRect, operation: NSCompositeSourceOver, fraction: 1.0)
    NSGraphicsContext.currentContext = old_context
  end 

  # check if this layer is colliding with another rect
  def collide_with?(other_rect)
    NSIntersectsRect(rect_version, other_rect)
  end
  
  # Rect version of our layer
  def rect_version
    NSMakeRect(
      @x - 0.5 * @width,
      @y - 0.5 * @height,
      @width, @height)
  end
  
end