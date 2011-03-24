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
class NSButton

  # wrapper to let you easily change the font size of a button's title
  def title_font_size=(size)
    color =  self.attributedTitle.attribute( NSForegroundColorAttributeName, atIndex: 0, effectiveRange: nil)
    current_font = self.attributedTitle.attribute( NSFontAttributeName, atIndex: 0, effectiveRange: nil)
    font = NSFont.fontWithName(current_font.fontName, size:size)
      
    opts = { NSForegroundColorAttributeName => color, NSFontAttributeName => font }
    self.attributedTitle = NSAttributedString.alloc.initWithString( self.title, attributes: opts)
  end
  
  def title_color=(color)
    current_font = self.attributedTitle.attribute( NSFontAttributeName, atIndex: 0, effectiveRange: nil)
    opts = { NSForegroundColorAttributeName => color, NSFontAttributeName => current_font }
    self.attributedTitle = NSAttributedString.alloc.initWithString( self.title, attributes: opts)
  end
  
end