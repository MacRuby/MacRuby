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
    HotCocoaApplication.instance = self
    @controllers = {}
    @shared_application = application
    @application_controller = controller(:application_controller)
    shared_application.delegate = application_controller
  end
  
  def start
    @shared_application.run
  end
  
  def controller(controller_name)
    controller_class = Object.const_get(controller_name.to_s.camel_case)
    @controllers[controller_name] || create_controller_instance(controller_name, controller_class)
  end
  
  private
  
    def create_controller_instance(controller_name, controller_class)
      controller_instance = controller_class.new(self)
      @controllers[controller_name] = controller_instance
      controller_instance.application_window
      controller_instance
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
  
  def application_window
    @application.application_controller.application_window
  end

end

class HotCocoaApplicationController < HotCocoaController
  
  def initialize(application)
    super(application)
  end
  
  def application_window
    @application_window ||= ApplicationWindow.new(self).application_window
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
  
  attr_reader :application_controller, :application_window
  
  def initialize(application_controller)
    @application_controller = application_controller
    render
  end
  
  def render
    @application_window = HotCocoa.window(:title => title)
    @application_window.view << application_controller.application_view
  end
  
  def title
    "My Application"
  end
  
end

class HotCocoaView < NSView
  
  DefaultLayoutOptions = {:expand => [:width, :height]}
  
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
        view.setup_view
        view
      end
    }, __FILE__, __LINE__
  end
  
  attr_reader :controller

  def setup_view
    @controller = class_controller
    self.layout = layout_options
    render
  end
  
  private
  
    def class_controller
      HotCocoaApplication.instance.controller(self.class.controller)
    end
  
    def layout_options
      options = if self.class.options && self.class.options[:layout]
        self.class.options[:layout]
      else
        DefaultLayoutOptions
      end
    end

end

class ApplicationWindow < HotCocoaWindow
  
end

