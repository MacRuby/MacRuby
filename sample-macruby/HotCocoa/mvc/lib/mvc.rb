require 'hotcocoa'

class String
  
  def underscore
    to_s.gsub(/::/, '/').gsub(/([A-Z]+)([A-Z][a-z])/,'\1_\2').gsub(/([a-z\d])([A-Z])/,'\1_\2').tr("-", "_").downcase
  end #unless defined?(:underscore)
  
  def camel_case
    if self !~ /_/ && self =~ /[A-Z]+.*/
      self
    else
      split('_').map{|e| e.capitalize}.join
    end
  end #unless defined?(:camel_case)
end

class HotCocoaApplication
  attr_accessor :shared_application, :application_controller, :controllers
  
  include HotCocoa
  
  def self.instance=(instance)
    @instance = instance
  end
  
  def self.instance
    @instance
  end
  
  def initialize
    @controllers = {}
    @shared_application = application
    @application_controller = controller(:application_controller)
    HotCocoaApplication.instance = self
    shared_application.delegate = application_controller
  end
  
  def start
    @shared_application.run
  end
  
  def controller(controller_name)
    controller_class = Object.const_get(controller_name.to_s.camel_case)
    @controllers[controller_name] ||= controller_class.new(self)
  end
  
end

class HotCocoaController
  
  def self.view_instances
    @view_instances ||= {}
  end
  
  attr_reader :application
  
  def initialize(application)
    @application = application
  end
  
  def main_window
    @application.application_controller.main_window
  end

end

class HotCocoaApplicationController < HotCocoaController
  
  def initialize(application)
    super(application)
    @main_window = ApplicationWindow.new(self)
  end
  
  def main_window
    @main_window
  end
  
  # help menu item
  def on_help(menu)
  end

  # This is commented out, so the minimize menu item is disabled
  def on_minimize(menu)
  end

  # window/zoom
  def on_zoom(menu)
  end

  # window/bring_all_to_front
  def on_bring_all_to_front(menu)
  end

end

class HotCocoaWindow
  
  attr_reader :application_controller, :main_window
  
  include HotCocoa
  
  def initialize(application_controller)
    @application_controller = application_controller
    render
  end
  
  def render
    @main_window = window(:title => title, :view => application_controller.application_view)
  end
  
  def title
    "My Application"
  end
  
end

class HotCocoaView < NSView
  
  module ClassMethods
    def controller(name=nil)
      if name
        @name = name
      else
        @name || :application_controller
      end
    end
    def options(options=nil)
      @options = options if options
    end
  end
  
  def self.inherited(klass)
    klass.extend(ClassMethods)
    klass.send(:include, HotCocoa::Behaviors)
    class_name = klass.name.underscore
    HotCocoaController.class_eval %{
      def #{class_name}
        view = HotCocoaController.view_instances[:#{class_name}] ||= #{klass.name}.alloc.initWithFrame([0,0,0,0])
        puts view.inspect
        view
      end
    }, __FILE__, __LINE__
  end
  
  attr_reader :controller

  def initialize
    @controller = HotCocoaApplication.instance.controller(self.class.controller)
    render
  end
  
end


