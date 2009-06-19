require 'hotcocoa'

class HotCocoaApplication
  attr_accessor :shared_application, :application_controller, :controllers
  
  include HotCocoa
  
  def self.instance=(instance)
    @instance = instance
  end
  
  def self.instance
    @instance
  end
  
  def initialize(application_file)
    HotCocoaApplication.instance = self
    @controllers = {}
    load_controllers_and_views(directory_of(application_file))
    @shared_application = application(ApplicationView.options[:application])
    @shared_application.load_application_menu
    @application_controller = controller(:application_controller)
    shared_application.delegate_to(application_controller)
  end
  
  def start
    @shared_application.run
  end
  
  def controller(controller_name)
    controller_name_string = controller_name.to_s
    controller_class = Object.const_get(controller_name_string !~ /_/ && controller_name_string =~ /[A-Z]+.*/ ? controller_name_string : controller_name_string.split('_').map{|e| e.capitalize}.join)
    @controllers[controller_name] || create_controller_instance(controller_name, controller_class)
  end
  
  private
  
    def create_controller_instance(controller_name, controller_class)
      controller_instance = controller_class.new(self)
      @controllers[controller_name] = controller_instance
      controller_instance.application_window
      controller_instance
    end
    
    def directory_of(application_file)
      File.dirname(File.expand_path(application_file))
    end
    
    def load_controllers_and_views(directory)
      Dir.glob(File.join(directory, 'controllers', '**', '*.rb')).each do |controller_file|
        load(controller_file)
      end
      Dir.glob(File.join(directory, 'views', '**', '*.rb')).each do |view_file|
        load(view_file)
      end
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

end

class HotCocoaWindow
  
  attr_reader :application_controller, :application_window
  
  def initialize(application_controller)
    @application_controller = application_controller
    render
  end
  
  def render
    @application_window = HotCocoa.window(ApplicationView.options[:window])
    @application_window.delegate_to(application_controller)
    @application_window.view << application_controller.application_view
  end
  
end

class HotCocoaView < HotCocoa::LayoutView
  
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
      if options
        @options = options
      else
        @options
      end
    end
  end
  
  def self.inherited(klass)
    klass.extend(ClassMethods)
    klass.send(:include, HotCocoa::Behaviors)
    class_name = klass.name.gsub(/::/, '/').gsub(/([A-Z]+)([A-Z][a-z])/,'\1_\2').gsub(/([a-z\d])([A-Z])/,'\1_\2').tr("-", "_").downcase
    HotCocoaController.class_eval %{
      def #{class_name}
        unless HotCocoaController.view_instances[:#{class_name}]
          HotCocoaController.view_instances[:#{class_name}] = #{klass.name}.alloc.initWithFrame([0,0,0,0])
          HotCocoaController.view_instances[:#{class_name}].setup_view
        end
        HotCocoaController.view_instances[:#{class_name}]
      end
    }, __FILE__, __LINE__
  end
  
  attr_reader :controller

  def setup_view
    unless @controller
      @controller = class_controller
      self.layout = layout_options
      render
    end
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

class Application < HotCocoaApplication
  
end
