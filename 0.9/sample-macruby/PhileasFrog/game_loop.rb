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

# The GameLoop is a key part of the video game, without it, the graphics 
# don't change and nothing happens.
# 
# This game loop offers 3 modes:
#
# * normal refresh (play mode)
# * no refresh (game over mode)
# * pause action refresh (only the player and its vehicle are redrawn, like in a level change)
#

module GameLoop
  
  def start_refreshing
    puts "start refreshing"
    @timer = NSTimer.scheduledTimerWithTimeInterval GameData::GAME_UPDATE_DURATION,
                                           target: self,
                                           selector: 'refresh_screen:',
                                           userInfo: nil,
                                           repeats: true
  end 
  
  # when we pause the action, the player can still move around.
  # if a block is being passed, it will be called when the action is resumed
  def pause_action(duration=nil, &block)
    stop_refreshing
    @post_pause_action = block if block_given?
    @pause_duration = duration.nil? ? nil : (duration * (GameData::GAME_UPDATE_DURATION * 500))
    hide_falling_items
    @pause_timer = NSTimer.scheduledTimerWithTimeInterval GameData::GAME_UPDATE_DURATION,
                                           target: self,
                                           selector: 'refresh_pause_screen:',
                                           userInfo: nil,
                                           repeats: true
  end
  
  def refresh_pause_screen(timer=nil)
    GameData.player_layer.update
    GameData.vehicle_layer.update
    @pause_duration -= 1 unless @pause_duration.nil?
    puts "waiting..."
    restart_action if @pause_duration.zero?
  end
  
  def restart_action
    if @post_pause_action
      @post_pause_action.call && @post_pause_action = nil 
    end
    @pause_timer.invalidate && @pause_timer = nil if @pause_timer
    start_refreshing
  end
  
  def refresh_screen(timer=nil)
    GameData.all_layers.each{ |layer| layer.update }
    collided_bombs, collided_rubies = GameData.collisions
    if !collided_bombs.empty?
      puts "crash"
      loose_a_life
      collided_bombs.each{|layer| layer.item.reset! }
    else
      collided_rubies.each do |layer|
        puts "collided with #{layer}"
        GameData.increase_points(layer.item.points)
        points.attributedStringValue = GameData.points.to_s
        layer.item.reset!
      end
      SoundEffects.frog(0.2) unless collided_rubies.empty?
      level_change! if change_level?
    end
  end
  
  def stop_refreshing
    @timer.invalidate && @timer = nil if @timer
  end
    
  def pause
    GameData.toggle_pause ? stop_refreshing : start_refreshing
  end

end