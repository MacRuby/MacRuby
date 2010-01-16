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

# This module offers a series of CoreAnimation effects to apply on
# any layers. You can also easily wrap all the various CIFilters

module VisualEffects
  module_function
  
  def fade_out(layer, duration = 0.2, to = 0.0)
    fade(layer, duration, to)
  end
  
  def fade(layer, duration = 0.5, to = 0.2)
    CATransaction.begin
      CATransaction.setValue 0.5, forKey: 'animationDuration'

      GameData.player_layer
      # fade it out
      fadeAnimation = CABasicAnimation.animationWithKeyPath "opacity"
      fadeAnimation.toValue = to
      fadeAnimation.timingFunction = CAMediaTimingFunction.functionWithName('easeIn') 
      layer.addAnimation fadeAnimation, forKey:"fadeAnimation"
    CATransaction.commit
  end
  
  def glow(layer, duration=nil)
    filter = CIFilter.filterWithName("CIBloom")
    filter.setDefaults
    filter.setValue(5.0, forKey: "inputRadius")
 
    # name the filter so we can use the keypath to animate the inputIntensity
    # attribute of the filter
    filter.name = "pulseFilter"
 
    # set the filter to the selection layer's filters
    layer.setFilters([filter])
 
    # create the animation that will handle the pulsing.
    pulseAnimation = CABasicAnimation.animation
 
#     # the attribute we want to animate is the inputIntensity
#     # of the pulseFilter
#     pulseAnimation.keyPath = "filters.pulseFilter.inputIntensity";
#  
#     # we want it to animate from the value 0 to 1
#     pulseAnimation.fromValue = 0.0
#     pulseAnimation.toValue = 1.5
#   
#     # over a one second duration, and run 100 times
#     pulseAnimation.duration = 1.0;
#     pulseAnimation.repeatCount = 100
#  
#     # we want it to fade on, and fade off, so it needs to
#     # automatically autoreverse.. this causes the intensity
#     # input to go from 0 to 1 to 0
#     pulseAnimation.autoreverses = true
#  
#     # use a timing curve of easy in, easy out..
#     pulseAnimation.timingFunction = CAMediaTimingFunction.functionWithName('easeInEaseOut')
 
    # add the animation to the layer. This causes
    # it to begin animating. We'll use pulseAnimation as the
    # animation key name
    layer.addAnimation(pulseAnimation, forKey:"pulseAnimation")
  end
  
end
