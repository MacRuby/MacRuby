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

# a GameItem instance is designed to live on an ImageLayer instance
# it holds all the characteristics needed for any game items.
# Coordinates are kept proportional so we can scale up and down the display.

class GameItem
  attr_accessor :image_name, :angle, :x, :y, :width, :height, :speed, :visible
  
  def initialize(x, y, width, height, visible=true, image_name=nil)
		@image_name = image_name
		@angle = 0
		@x = x || 0
		@y = y || 0
		@width    = width
		@height   = height
		@visible  = visible
		@speed    = 0
	end
  
end