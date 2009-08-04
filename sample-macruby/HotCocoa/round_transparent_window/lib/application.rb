require 'hotcocoa'
SOURCE_DIR = File.expand_path(File.dirname(__FILE__))
require SOURCE_DIR + '/nib_controller'
require SOURCE_DIR + '/controller'
require SOURCE_DIR + '/custom_view'
require SOURCE_DIR + '/custom_window'

# Replace the following code with your own hotcocoa code

class Application

  include HotCocoa
  
  def start
    application :name => "HotCocoa: Round Transparent Window" do |app|
      app.delegate = self
      # load our nib
      NibController.new
    end
  end
  
  # file/open
  def on_open(menu)
  end
  
  # file/new 
  def on_new(menu)
  end
  
  # help menu item
  def on_help(menu)
  end
  
  # This is commented out, so the minimize menu item is disabled
  #def on_minimize(menu)
  #end
  
  # window/zoom
  def on_zoom(menu)
  end
  
  # window/bring_all_to_front
  def on_bring_all_to_front(menu)
  end
  
end

Application.new.start