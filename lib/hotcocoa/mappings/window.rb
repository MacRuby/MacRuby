# Cocoa Reference: NSWindow
#
# Usage example:
# ==============
#
#   window :frame => [100, 100, 604, 500], :title => "My app", :style => [:titled, :closable, :miniaturizable, :resizable] do |win|
#     win.contentView.margin  = 0
#     win.background_color    = color(:name => 'white')  
#     win.will_close { exit }
#   end
#
# Apple Developer Connection url: http://developer.apple.com/documentation/Cocoa/Reference/ApplicationKit/Classes/NSWindow_Class/Reference/Reference.html
#

HotCocoa::Mappings.map :window => :NSWindow do
    
  defaults  :style => [:titled, :closable, :miniaturizable, :resizable],
            :backing => :buffered, 
            :defer => true,
            :show => true,
            :view => :layout,
            :default_layout => {}
            
  constant :backing, :buffered => NSBackingStoreBuffered
  
  constant :style,   {
    :borderless         => NSBorderlessWindowMask, 
    :titled             => NSTitledWindowMask, 
    :closable           => NSClosableWindowMask, 
    :miniaturizable     => NSMiniaturizableWindowMask, 
    :resizable          => NSResizableWindowMask,
    :textured           => NSTexturedBackgroundWindowMask
  }

  def init_with_options(window, options)
    unless options[:frame]
      size = (options.delete(:size) || [400,400])
      width, height = size
      screen_frame = NSScreen.screens[0].visibleFrame
      center = options.delete(:center)
      x = screen_frame.origin.x + (center ? (screen_frame.size.width - width)/2    : 30)
      y = screen_frame.origin.y + (center ? (screen_frame.size.height - height)/2 : (screen_frame.size.height - height - 30))
      options[:frame] = [x, y, width, height]
    end
    window = window.initWithContentRect options.delete(:frame), 
                               styleMask:options.delete(:style), 
                               backing:options.delete(:backing), 
                               defer:options.delete(:defer)
    default_layout = options.delete(:default_layout)
    if options[:view] == :layout
      options.delete(:view)
      window.setContentView(HotCocoa::LayoutView.alloc.initWithFrame([0,0,window.contentView.frameSize.width, window.contentView.frameSize.height]))
      window.contentView.default_layout = default_layout
    elsif options[:view] == :nolayout
      options.delete(:view)
    end
    window
  end
    
  custom_methods do
    
    def <<(control)
      contentView.addSubview control
    end
    
    def view
      contentView
    end

    def view=(view)
      setContentView(view)
    end

    def on_notification(options={}, &block)
      options[:sent_by] = self
      NotificationListener.new(options, &block)
    end
    
    def show
      display
      makeKeyAndOrderFront(nil)
      orderFrontRegardless
    end
    
    def has_shadow?
      hasShadow
    end
    
  end
  
  delegating "window:shouldDragDocumentWithEvent:from:withPasteboard:", :to => :should_drag_document?,    :parameters => [:shouldDragDocumentWithEvent, :from, :withPasteboard]
  delegating "window:shouldPopUpDocumentPathMenu:",                     :to => :should_popup_path_menu?,  :parameters => [:shouldPopUpDocumentPathMenu]
  delegating "window:willPositionSheet:usingRect:",                     :to => :will_position_sheet,      :parameters => [:willPositionSheet, :usingRect]
  delegating "windowDidBecomeKey:",                                     :to => :did_become_key
  delegating "windowDidBecomeMain:",                                    :to => :did_become_main
  delegating "windowDidChangeScreen:",                                  :to => :did_change_screen
  delegating "windowDidChangeScreenProfile:",                           :to => :did_change_screen_profile
  delegating "windowDidDeminiaturize:",                                 :to => :did_deminiturize
  delegating "windowDidEndSheet:",                                      :to => :did_end_sheet
  delegating "windowDidExpose:",                                        :to => :did_expose,               :parameters => ["windowDidExpose.userInfo['NSExposedRect']"]
  delegating "windowDidMiniaturize:",                                   :to => :did_miniaturize
  delegating "windowDidMove:",                                          :to => :did_move
  delegating "windowDidResignKey:",                                     :to => :did_resign_key
  delegating "windowDidResignMain:",                                    :to => :did_resign_main
  delegating "windowDidResize:",                                        :to => :did_resize
  delegating "windowDidUpdate:",                                        :to => :did_update
  delegating "windowShouldClose:",                                      :to => :should_close?
  delegating "windowShouldZoom:toFrame:",                               :to => :should_zoom?,             :parameters => [:toFrame]
  delegating "windowWillBeginSheet:",                                   :to => :will_begin_sheet
  delegating "windowWillClose:",                                        :to => :will_close
  delegating "windowWillMiniaturize:",                                  :to => :will_miniaturize
  delegating "windowWillMove:",                                         :to => :will_move
  delegating "windowWillResize:toSize:",                                :to => :will_resize,              :parameters => [:toSize]
  delegating "windowWillReturnFieldEditor:toObject:",                   :to => :returning_field_editor,   :parameters => [:toObject]
  delegating "windowWillReturnUndoManager:",                            :to => :returning_undo_manager
  delegating "windowWillUseStandardFrame:defaultFrame:",                :to => :will_use_standard_frame,  :parameters => [:defaultFrame]

end