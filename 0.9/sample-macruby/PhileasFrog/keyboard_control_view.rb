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

# This NSView subclass receives and handles the key controls

class KeyboardControlView < NSView
  attr_accessor :game_controller
  
  ESCAPE_KEY_CODE = 27
  SPACE_KEY_CODE  = 32
  P_KEY_CODE      = 112
  X_KEY_CODE      = 120
  Q_KEY_CODE      = 113
  ENTER_KEY_CODE  = 13
  
  def acceptsFirstResponder
    true
  end

  # Deals with keyboard keys being pressed   
  def keyDown(event)
	  characters = event.characters
    if characters.length == 1 && !event.isARepeat
      character = characters.characterAtIndex(0)
      if GameData.game_on?
        if character == NSLeftArrowFunctionKey
          PlayerPosition.left if GameData.player
        elsif character == NSRightArrowFunctionKey
          PlayerPosition.right if GameData.player
        elsif character == NSUpArrowFunctionKey
          puts "UP pressed"
        elsif character == NSDownArrowFunctionKey
          puts "DOWN pressed"
        elsif character == SPACE_KEY_CODE || character == P_KEY_CODE
          game_controller.pause
        elsif character == ESCAPE_KEY_CODE || character == X_KEY_CODE
          game_controller.toggle_fullscreen(nil)
        elsif character == Q_KEY_CODE
          exit
        elsif character == ENTER_KEY_CODE && GameData.game_on?
          # play!
        end
  		elsif character == ESCAPE_KEY_CODE || character == X_KEY_CODE
  		  game_controller.toggle_fullscreen(nil)
      elsif character == Q_KEY_CODE
        exit
      else
       # puts character
      end
    end
    # super
  end

  # Deals with keyboard keys being released   
  def keyUp(event)
	  characters = event.characters
    if GameData.game_on? && characters.length == 1
      character = characters.characterAtIndex(0)
  		if character == NSLeftArrowFunctionKey
        PlayerPosition.release_left
  		elsif character == NSRightArrowFunctionKey
        PlayerPosition.release_right
  		elsif character == NSUpArrowFunctionKey
  		  puts "UP released"
      elsif character == NSDownArrowFunctionKey
  		  puts "DOWN released"
  		elsif character == ESCAPE_KEY_CODE
  		  puts "ESCAPE released"
      end  
    end
    # super
  end
    
end