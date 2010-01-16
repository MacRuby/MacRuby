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

class FallingRuby < GameItem
  attr_accessor :waiting_cycle
  attr_reader :points
  
  @min_size = 0.05
	START_RADIUS = 0.40
	MIN_SPEED = 0.033
	MAX_SPEED = 0.166
  WAITING_CYCLES = (2..100).to_a
  IMAGES = ['ruby', 'ruby-black']
  
  def initialize(*args)
    super
    @visible = false
    @waiting_cycle = random_waiting_cycle
    @speed = random_speed
    @points = 10
  end
  
  def self.min_size
    @min_size
  end
  
  def self.random_size(size=nil)
    size ||= @min_size
    size + (rand(size) / 20.0)
  end
  
  # Rubies shouldn't all fall at the same time
  # and at the same speed, so we are injecting some
  # randomness.
  def random_speed
    MIN_SPEED + (MAX_SPEED - MIN_SPEED) * rand
  end
  
  # when a ruby is being reset, let's randomly wait before
  # we make it appear again.
  def random_waiting_cycle
    WAITING_CYCLES[rand(WAITING_CYCLES.size + 1)]
  end
  
  # calculate if the item is out of sight
  def out_of_sight?
    return true if y == nil || height == nil
    y < -(height*2) || y > (1 + (height*2))
  end
  
  def reset!
    @visible = false
    @x = rand(16)/10.0
    @y = 1 + height
    @speed = random_speed
    @waiting_cycle = random_waiting_cycle
  end
  
  # being called from the game loop
  # the item gets reset if it's not on screen anymore.
  # Depending on its visibility and the queuing
  # the instance will change coordinates and visibility
  def update
    reset! if out_of_sight?
    if visible
      @y = y - (0.3 * speed)
    elsif waiting_cycle && waiting_cycle > 0
      @waiting_cycle -= 1
    else
      @visible = true
    end
  end
  
   
end