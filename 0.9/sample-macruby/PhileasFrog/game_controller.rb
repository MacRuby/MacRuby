#
#  game_controller.rb
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
# 
# The game controller as you might have guessed controls the game
# it has access to the visual elements as well as the memory/data


# The game loop is also controlled by the controller 
require 'game_loop'
require 'game_config'

class GameController
  include GameLoop

  # outlets
  attr_accessor :content_view
  attr_accessor :button_view
  attr_accessor :status_view
  attr_reader   :background_layer
  attr_accessor :points
  attr_accessor :level_title
  attr_accessor :lives
  
  attr_accessor :fullscreen_button
  attr_accessor :new_game_button
  attr_accessor :exit_button
  

  def initialize
    @background_layer = CALayer.layer
  end

  # Called when the nib/xib(view) is ready
  def awakeFromNib
    status_view.hidden = true
    GameData.register_controller(self)
    # set the background to be black
    content_view.layer.backgroundColor = CGColorCreateGenericRGB(0, 0, 0, 1)
    CGColorRelease(content_view.layer.backgroundColor)
    # create a new CALayer with a background image
    @background_layer = ImageLayer.alloc.initWithImageNamed('intro_bg')
    @background_layer.refresh
    @background_layer.masksToBounds = true
    content_view.layer.insertSublayer(@background_layer, atIndex: 0)
    # adding an observer to call our method when NSViewFrameDidChangeNotification is triggered
    NSNotificationCenter.defaultCenter.addObserver( self, 
                                                    selector: 'resize:',
                                                    name: NSViewFrameDidChangeNotification,
                                                    object: content_view)  
    setup_intro_text
    resize(nil)
  end
  
  # when the window is being closed, close the game
  # this is called because we setup a delegation from the window to this controller
  def windowWillClose(sender)
   exit
  end
  
  # sets the font and the text color
  # of the displayed text
  def setup_intro_text
    font = NSFont.fontWithName('akaDylan Plain', size:24)
    lives.font = font
    points.font = font
    level_title.font = font
    
    new_game_button.title_color   = NSColor.whiteColor
    fullscreen_button.title_color = NSColor.whiteColor
    exit_button.title_color       = NSColor.whiteColor
  end
  
  # adds an item on a new layer
  # and returns the newly created image layer
  def display_item(item)
    new_layer = ImageLayer.alloc.initWithItem(item)
    GameData.all_layers << new_layer
    CATransaction.begin
      CATransaction.setValue(true, forKey: 'disableActions')
      @background_layer.addSublayer(new_layer)
	  CATransaction.commit
    GameData.clear_layer_caches
    new_layer
  end
  
  # set remove_from_game_data as false if you are looping
  # through the layers and removing them one by one
  # and want to avoid modifying an the array while iterating it
  def remove_layer(layer, remove_from_game_data = true)
    found_layer = GameData.all_layers.detect{|l| l == layer}
    if found_layer
      found_layer.removeFromSuperlayer if layer.respond_to?(:removeFromSuperlayer)
      GameData.all_layers.delete(found_layer) if remove_from_game_data
      background_layer.refresh
    end
    GameData.clear_layer_caches
  end
  
  # gets triggered by the new game button
  def new_game(sender)    
    button_view.hidden = true
    status_view.hidden = false
    # reset the lives
    lives.attributedStringValue = GameConfig.starting_lives.to_s
	  content_view.becomeFirstResponder
    # setup the new game objects
    background_layer.change_image("background_level_1")
    GameData.restart_game!
    GameData.setup_new_game
    # start the game loop
    start_refreshing
    GameData.game_on!
    set_game_level_display
  end

  # When the window (and hence the window's content view) changes size, this
  # will resize, reposition and rescale the content by adjusting the
  # background_layer (to which all other CALayers are parented).
  #
  def resize(notification)
    p "resizing"
    game_width =  GameData.game_width
    game_height = GameData.game_height
    aspect_size = content_size = content_view.bounds.size

    if (aspect_size.width / aspect_size.height) > (game_width / game_height)
      scale = aspect_size.height / game_height
      aspect_size.width = aspect_size.height * (game_width / game_height)  
    else
      scale = aspect_size.width / game_width
      aspect_size.height = aspect_size.width * (game_height / game_width);
    end
        
    if fullscreen_button.isHidden
      game_over_buttons
    else
      font_size     = 48 * scale
      button_width  = 400 * scale
      button_height = 80 * scale
      button_x      = 200 * scale
      button_ys     = [440, 360, 280]
      
      fullscreen_button.title_font_size = font_size
      new_game_button.title_font_size   = font_size
      exit_button.title_font_size       = font_size
      
      new_game_button.frame   = [[button_x, (button_ys[0] * scale)],  [button_width, button_height]]    
      fullscreen_button.frame = [[button_x, (button_ys[1] * scale)],  [button_width, button_height]]
      exit_button.frame       = [[button_x, (button_ys[2] * scale)],  [button_width, button_height]]
    end
    
    CATransaction.begin
      CATransaction.setValue(true, forKey: 'disableActions')
      background_layer.transform = CATransform3DMakeScale(scale, scale, 1.0);
      background_layer.frame = CGRectMake( 0.5 * (content_size.width - aspect_size.width),
                                           0.5 * (content_size.height - aspect_size.height),
                                                  aspect_size.width, aspect_size.height)
    CATransaction.commit
    content_view.becomeFirstResponder
  end
  
  # called when the fullscreen button is called
  def toggle_fullscreen(sender)
  	if content_view.isInFullScreenMode
		  content_view.exitFullScreenModeWithOptions(nil)
	  else
		  content_view.enterFullScreenMode(content_view.window.screen, withOptions:nil)
		end
		
		content_view.subviews.each do |view|
			view.removeFromSuperview
			content_view.addSubview(view)
		end
    
    content_view.window.makeFirstResponder(content_view)
  end
  
  def change_level?
    GameData.points >= GameData.max_score_for(GameData.game_level)
  end
  
  def level_change!
    puts "LEVEL CHANGE"
    GameData.game_level += 1
    background_layer.change_image("splash_#{GameData.game_level}")
    
    pause_action(2) do
      GameData.all_layers.each{|layer| remove_layer(layer, false) unless layer.item.is_a?(Player) }
      GameData.all_layers.delete_if{|layer| !layer.item.is_a?(Player)}
      GameData.level_up!
      upgrade_level_graphics
      GameData.reset_player_layer
    end
  end
  
  def loose_a_life
    SoundEffects.bomb(0.1)
    VisualEffects.fade_out(GameData.player_layer)
    if GameData.lives == 1
      game_over
    else
      GameData.lives -= 1
      puts "special effect here"
    end
    set_live_display
  end
  
  def game_over
    puts "Game Over"
    game_over_buttons
    GameData.lives =  0
    background_layer.change_image("game_over")
    refresh_screen
    hide_all_items
    GameData.restart_game!
    update_points_graphics
    stop_refreshing
    GameData.game_off!
    button_view.hidden = false
    status_view.hidden = true 
  end
  
  def set_game_level_display
    level_title.attributedStringValue = "Level #{GameData.game_level}"
  end
  
  def set_live_display
    lives.attributedStringValue = GameData.lives.to_s
  end
  
  def game_over_buttons
    scale = content_view.bounds.size.width / GameData.game_width
    
    fullscreen_button.hidden = true
    new_game_button.title = 'Try Again'
    new_game_button.title_color = NSColor.whiteColor
    new_game_button.title_font_size = 48 * scale
    exit_button.title_font_size = 48 * scale
    new_game_button.frame = [[(300 * scale), (200 * scale)],[(400 * scale), (80 * scale)]]
    exit_button.frame     = [[(760 * scale), (200 * scale)],[(400 * scale), (80 * scale)]]
  end
  
  # some layers need to be refreshed and updated when changing level
  # this is where we do that.
  def upgrade_level_graphics
    set_game_level_display
    set_live_display
    update_points_graphics
    level_config = GameConfig.data[:levels][GameData.game_level - 1]
    background_layer.change_image("background_level_#{GameData.game_level}")
    GameData.player_layer.change_image("frog_level_#{GameData.game_level}", level_config[:player_width], level_config[:player_height])    
    GameData.vehicle_layer.change_image(level_config[:vehicle])
  end
  
  def update_points_graphics
    points.attributedStringValue = GameData.points.to_s
  end
  
  def hide_falling_items
    GameData.ruby_layers.each{|layer| layer.hidden = true; layer.refresh}
    GameData.bomb_layers.each{|layer| layer.hidden = true; layer.refresh}
  end
  
  def hide_all_items
    GameData.all_layers.each{|layer| layer.hidden = true; layer.refresh}
  end

end  