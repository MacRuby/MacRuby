require 'hotcocoa'

framework 'WebKit'
framework 'QTKit'

include HotCocoa

# Replace the following code with your own hotcocoa code

class DemoApplication
  
  def self.register(view_class)
    view_classes << view_class
  end
  
  def self.view_classes
    @view_classes ||= []
  end
  
  def self.view_with_description(description)
    @view_classes.detect {|view| view.description == description}
  end
  
  attr_reader :current_demo_view, :main_window
  
  def start
    load_demo_files
    application(:name => "Demo") do |app|
      app.delegate = self
      
      add_status_bar_speech_menu_exammple

      # window example
      @main_window = window(:frame => [100, 100, 600, 500], :title => "HotCocoa Demo Application") do |win|
        win << window_geometry_label
        win << segment_control

        # can hook events on the window (mapped via delegate)
        win.will_close { exit }
        win.did_move { update_window_geometry_label }
        win.did_resize { update_window_geometry_label }
      end
      update_window_geometry_label
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
  
  private
  
    # status item and speech example
    def add_status_bar_speech_menu_exammple
      m = menu do |main|
        main.item :speak, 
                  :on_action => proc { speech_synthesizer.speak('I have a lot to say.') }
      end
      status_item :title => 'Speech Demo', :menu => m
    end
  
    def segment_control
      @segment_control ||= create_segment_control
    end
    
    # segmented control example
    def create_segment_control
      segmented_control(:layout => {:expand => :width, :align => :center, :start => false}, :segments => demo_app_segments) do |seg|
        seg.on_action do
          demo(@segment_control.selected_segment.label)
        end      
      end
    end
    
    def demo_app_segments
      DemoApplication.view_classes.collect {|view_class| {:label => view_class.description, :width => 0}}
    end
      
    def window_geometry_label
      @window_geometry_label ||= create_window_geometry_label
    end
  
    # label example with custom font
    def create_window_geometry_label
      label(:text => "", :layout => {:expand => :width, :start => false}, :font => font(:system => 15))
    end
    
    def update_window_geometry_label
      frame = main_window.frame
      window_geometry = "x=#{frame.origin.x}, y=#{frame.origin.y}, width=#{frame.size.width}, height=#{frame.size.height}"
      window_geometry_label.text = "Window frame: (#{window_geometry})"
    end
    
    def demo(description)
      main_window.view.remove(current_demo_view) if current_demo_view
      @current_demo_view = DemoApplication.view_with_description(description).create
      main_window << @current_demo_view
    end

    def load_demo_files
      Dir.glob(File.join(File.dirname(__FILE__), 'views', '*.rb')).each do |file|
        load file
      end
    end
end

DemoApplication.new.start