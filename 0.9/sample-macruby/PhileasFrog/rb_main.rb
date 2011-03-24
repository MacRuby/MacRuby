#  rb_main.rb
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

# Loading the Cocoa framework. If you need to load more frameworks, you can
# do that here too.
framework 'Cocoa'
framework 'QuartzCore'

puts NSBundle.mainBundle.localizations

# Loading all the Ruby project files.
dir_path = NSBundle.mainBundle.resourcePath.fileSystemRepresentation
$LOAD_PATH.unshift(dir_path)
load_later = []
Dir.entries(dir_path).each do |path|
  if path != File.basename(__FILE__) and File.extname(path) =~ /\.rb/
    require(path)
  elsif File.extname(path) == '.ttf'
    # register all the fonts (.ttf) from the resources folder
    puts "registring font: #{path}"
    font_name = File.basename(path, '.ttf')
    font_location = NSBundle.mainBundle.pathForResource(font_name, ofType: 'ttf')
    font_url = NSURL.fileURLWithPath(font_location)    
    # in MacRuby, always make sure that cocoa constants start by an uppercase
    CTFontManagerRegisterFontsForURL(font_url, KCTFontManagerScopeProcess, nil)
  end
end

# Starting the Cocoa main loop.
NSApplicationMain(0, nil)
NSApp.activateIgnoringOtherApps(true)