require 'hotcocoa'
require 'lib/mvc'

class Application < HotCocoaApplication
  
end


class ApplicationController < HotCocoaApplicationController
  #def switch_views
  #  main_window.view = my_other_view
  #end
end

class ApplicationWindow < HotCocoaWindow
  
end


class ApplicationView < HotCocoaView
  
  controller :application_controller
  
  options :layout => {:expand => [:width, :height]}
  
  def render 
    self << my_button
  end

  def my_button
    @my_button ||= button(:title => "Switch")#, :on_action => controller.method(:switch_views))
  end

end

# class MyController < HotCocoaController
#   def switch_back
#     window.view = application_view
#   end
# end
# 
# class MyOtherView < HotCocoaView
#   controller :my_controller
# 
#   def render 
#     view << my_button
#   end
# 
#   def my_button
#     @my_button ||= button(:title => "Switch", :on_action => :switch_back)
#   end
# end

Application.new.start