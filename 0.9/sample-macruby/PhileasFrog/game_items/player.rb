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
require 'game_item'

class Player < GameItem
  attr_accessor :acceleration
  attr_reader   :left_key_pressed
  attr_reader   :right_key_pressed
  
  SIZE = 0.1  
  MAX_SPEED = 0.05
  DEFAULT_SPEED = 0.04
  WIDTH = 0.2
  
  def initialize(*args)
    super
    @image_name ||= 'frog_level_1'
    @acceleration = 0.0
    @max_left = (@width/2)
    @max_right = (1.6 - @width)
    @left_key_pressed = false
    @right_key_pressed = false
    @width = WIDTH
    @height = 0.2
  end
  
  def update
    if @left_key_pressed
      move_left
    elsif @right_key_pressed
      move_right
    end  
  end
  
  def left
    @left_key_pressed = true
  end

  def release_left
    @left_key_pressed = false
    normal_speed
  end
    
  def move_left
    # if we are not at the left edge
    if x > (Player::WIDTH / 2)
      speed = @left_key_pressed ? accelerate : normal_speed
      @x -= speed
    end
    @angle = 0.05
  end
  
  def right
    @right_key_pressed = true
  end
  
  def release_right
    @right_key_pressed = false
    normal_speed
  end
  
  # When asked to move to the right
  # the coordinate is changed and the speed
  # changes depending on the fact that the user
  # is keeping the key pressed or not.
  # We are also slightly tilting the player.
  def move_right
    # 1.6 is the max proportional width
    if x < (1.6 - (Player::WIDTH / 1.5) )
      speed = @right_key_pressed ? accelerate : normal_speed
      @x += speed
    end
    @angle = -0.05
  end
  
  def accelerate
    @acceleration += 0.01 unless (@acceleration + DEFAULT_SPEED) >= MAX_SPEED
    DEFAULT_SPEED + acceleration
  end
  
  def normal_speed
    @angle = 0
    @acceleration = 0.0
    DEFAULT_SPEED
  end
         
end