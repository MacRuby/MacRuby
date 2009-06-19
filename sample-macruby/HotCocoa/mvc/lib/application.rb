require 'hotcocoa'
require 'lib/mvc'

class Application < HotCocoaApplication
  
end

class ApplicationController < HotCocoaApplicationController
  def switch_views(sender)
    application_window.view.remove(application_view)
    application_window.view << my_other_view
  end
end

class ApplicationView < HotCocoaView
  def render 
    self << HotCocoa.button(:title => "View 1!", :on_action => controller.method(:switch_views))
  end
end

class MyController < HotCocoaController
  def switch_back(sender)
    application_window.view.remove(my_other_view)
    application_window.view << application_view
  end

  def open_window(sender)
    HotCocoa.window(:center => true) << third_view
  end
  
end

class MyOtherView < HotCocoaView

  controller :my_controller

  def render 
    self << HotCocoa.button(:title => "View 2!", :on_action => controller.method(:switch_back), :frame => [0, 100, 200, 20])
    self << HotCocoa.button(:title => "open window!", :on_action => controller.method(:open_window))
  end

end

class ThirdController < HotCocoaController
  def close_window(sender)
    third_view.window.close
  end
end

class ThirdView < HotCocoaView

  controller :third_controller

  def render 
    self << HotCocoa.button(:title => "close window!", :on_action => controller.method(:close_window))
  end

end

Application.new.start