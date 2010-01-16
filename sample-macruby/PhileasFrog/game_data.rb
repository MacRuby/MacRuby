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

# The GameData module is the memory of the game, it knows where is who
# and keeps track of the score, lives etc.. as well as the display dimensions etc..

module GameData
  module_function
  
  attr_accessor :game_level, :lives
  attr_reader :points, :controller, :pause
  
  GAME_ASPECT = 1.6
  GAME_UPDATE_DURATION = 0.03
  @game_status = false
  
  def game_on?
    @game_status
  end
  
  def game_on!
    @game_status = true
  end
  
  def game_off!
    @game_status = false
  end
  
  # Hook to allow access to the controller from the GameData module
  def register_controller(controller)
    @controller = controller
  end
  
  def player
    @player_item ||= player_layer.item
  end
  
  def player_layer
    @player_layer ||= all_layers.find{|layer| layer.item.class.name == 'Player'} if all_layers
  end
  
  def reset_player_layer
    @player_layer = nil
  end
  
  def vehicle_layer
    @vehicle_layer ||= all_layers.find{|layer| layer.item.class.name == 'Vehicle'}
  end
  
  def vehicle
    @vehicle ||= vehicle_layer.item
  end
   
  def ruby_layers
    # don't forget to reset when changing level
    @ruby_layers ||= all_layers.select{|layer| layer.item.class.name == 'FallingRuby'}
  end
  
  def bomb_layers
    # don't forget to reset when changing level
    @bomb_layers ||= all_layers.select{|layer| layer.item.class.name == 'Bomb'}
  end
  
  def clear_layer_caches
    @ruby_layers, @bomb_layers = nil, nil
  end
   
  # Returns the width for the game area. Defaults to screen width.
  def game_width
    screen_size = NSScreen.mainScreen.frame.size
		if (screen_size.width / screen_size.height) > GAME_ASPECT
		  screen_size.width = screen_size.height * GAME_ASPECT
		end
		screen_size.width
  end
  
  # Returns the height for the game area. Defaults to screen height.
  def game_height
    screen_size = NSScreen.mainScreen.frame.size
		if (screen_size.width / screen_size.height) > GAME_ASPECT
			screen_size.height = screen_size.width / GAME_ASPECT
    end
		screen_size.height
  end
  
  def all_layers
    @all_layers ||= []
  end
  
  def add_item(item) 
    # the controller will create and send back the layers we need to store in all_layers
    controller.display_item(item)
  end
  
  def setup_new_game
    puts "new game"
    @points = 0
    @game_level = 1
    @lives = GameConfig.starting_lives
    
    # create objects
    setup_new_level(GameConfig.data[:levels].first)
    if player_layer.nil?
      puts "adding a new player item"
      add_item( Vehicle.new((GAME_ASPECT/2), 0.05, 0.1, 0.1) )
      add_item( Player.new((GAME_ASPECT/2), 0.15, Player::SIZE, Player::SIZE) )  # Player.new x, y, width, height, size
    end
  end
  
  def setup_new_level(config)
    raise "you passed an empty level config" if config == nil
    all_layers.delete_if{|layer| !layer.item.is_a?(Player) }

    clear_layer_caches
    
    # Rubies
    config[:rubies].times do
      height = FallingRuby.random_size
      width = height * config[:ruby_ratio]
      add_item FallingRuby.new(rand, 1, width, height, true, FallingRuby::IMAGES[rand(FallingRuby::IMAGES.size)])    
    end if config[:rubies]
    
    # Bombs (or whatever would
    config[:bombs].times do
      height = FallingRuby.random_size(Bomb.min_size)
      width = height * config[:bomb_ratio]
      add_item Bomb.new(rand, 1, width, height, true, config[:bomb_image])    
    end if config[:bombs]
    
    set_level_music
  end
  
  # lists all the layers colliding with the player layer
  # Returns [colliding bomb layers, colliding ruby layers]
  # the boolean is used to check if the user hit a bomb
  def collisions
    [collided_bombs, collided_rubies]
  end
  
  # Returns an array of ruby layers colliding with the player layer
  def collided_rubies
    ruby_layers.map do |ruby|
      ruby if ruby.collide_with?(GameData.player_layer.rect_version)
    end.compact
  end
  
  # Returns an array of bomb layers colliding with the player layer
  def collided_bombs
    bomb_layers.map do |bomb|
      bomb if bomb.collide_with?(GameData.player_layer.rect_version)
    end.compact
  end
  
  def level_up!
    puts "LEVEL UP!"
    level_data = GameConfig.data[:levels][(self.game_level - 1)]
    setup_new_level(level_data)
  end
  
  def restart_game!
    puts "reseting #{all_layers.size} layers"
    all_layers.each{|layer| controller.remove_layer(layer, false)}
    # reseting the caches
    @all_layers = []
    @player_layer, @vehicle_layer = nil, nil
    @player_item = nil
    @vehicle = nil
    @points = 0
  end
  
  def increase_points(points)
    @points += points
  end
  
  def max_score_for(level)
    GameConfig.data[:levels][(level - 1)][:score_limit]
  end
  
  def toggle_pause
    @pause ||= false
    @pause = !pause
  end
  
  def set_level_music
    puts "playing level_#{game_level}"
    @music.stop if @music
    sound_file = NSBundle.mainBundle.pathForResource("level_#{game_level}", ofType: 'mp3')
    if sound_file
      @music = NSSound.alloc.initWithContentsOfFile(sound_file, byReference: false)
      @music.loops = true
      @music.play
    end
  end
  
  protected
  
  def find_item_by_class(klass)
    all_layers.select{|layer| layer.item.class.name == klass}
  end

end