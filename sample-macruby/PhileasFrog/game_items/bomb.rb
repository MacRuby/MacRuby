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
require 'falling_ruby'

class Bomb < FallingRuby

  attr_accessor :waiting_cycle
  attr_reader :points
  attr_reader :min_size
  @min_size = 0.1
  
  def initialize(*args)
    super
    @visible = false
    @waiting_cycle = random_waiting_cycle
    @speed = random_speed
    @points = 10
  end

end