# player_position.rb
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

# Simple module encapsulating the player and positioning
# It currently doesn't do much expect from moving the vehicle
# at the same time as the player.
# However, we can imagine that in the future we will want to also move the 
# background or other items.

module PlayerPosition
  module_function

  # called when the user pressed the left keyboard control  
  def left
    GameData.player.left
    GameData.vehicle.left
  end
  
  # called when the user pressed the right keyboard control
  def right
    GameData.player.right
    GameData.vehicle.right
  end
  
  def release_right
    GameData.player.release_right
    GameData.vehicle.release_right
  end
  
  def release_left
    GameData.player.release_left
    GameData.vehicle.release_left
  end

end