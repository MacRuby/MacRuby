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

# Module that let's us play sound effects
# More tips about playing an audio file available on my blog: 
# http://merbist.com/2009/10/06/macruby-tips-how-to-play-an-audio-file/

module SoundEffects
 module_function
 
 # sounds
 @frog = NSSound.soundNamed "Frog"
 @bomb = NSSound.soundNamed "Basso"
 
 def frog(delay=0)
   play_object_with_delay(@frog, delay)
 end
 
 def bomb(delay=0)
   play_object_with_delay(@bomb, delay)
 end 
 
 def play_object_with_delay(object, delay)
   object.performSelector(:play, withObject: nil, afterDelay: delay)
 end
end