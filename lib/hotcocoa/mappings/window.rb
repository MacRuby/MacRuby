HotCocoa::Mappings.map :window => :NSWindow do
    
  defaults  :style => [:titled, :closable, :miniturizable, :resizable],
            :backing => :buffered, 
            :defer => true,
            :show => true,
            :view => :layout
            
  constant :backing, :buffered => NSBackingStoreBuffered
  
  constant :style,   {
    :borderless         => NSBorderlessWindowMask, 
    :titled             => NSTitledWindowMask, 
    :closable           => NSClosableWindowMask, 
    :miniturizable      => NSMiniaturizableWindowMask, 
    :resizable          => NSResizableWindowMask
  }

  def init_with_options(window, options)
    window.initWithContentRect options.delete(:frame), 
                               styleMask:options.delete(:style), 
                               backing:options.delete(:backing), 
                               defer:options.delete(:defer)
  end
    
  custom_methods do
    
    def <<(control)
      contentView.addSubview control
    end
    
    def view
      contentView
    end

    def view=(view)
      if view == :layout
        setContentView(LayoutView.alloc.initWithFrame([0,0,contentView.frameSize.width, contentView.frameSize.height]))
      else
        setContentView(view)
      end
    end
    
    def show
      display
      makeKeyAndOrderFront(nil)
      orderFrontRegardless
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
  delegating "windowDidMiniaturize:",                                   :to => :did_miniturize
  delegating "windowDidMove:",                                          :to => :did_move
  delegating "windowDidResignKey:",                                     :to => :did_resign_key
  delegating "windowDidResignMain:",                                    :to => :did_resign_main
  delegating "windowDidResize:",                                        :to => :did_resize
  delegating "windowDidUpdate:",                                        :to => :did_update
  delegating "windowShouldClose:",                                      :to => :should_close?
  delegating "windowShouldZoom:toFrame:",                               :to => :should_zoom?,             :parameters => [:toFrame]
  delegating "windowWillBeginSheet:",                                   :to => :will_begin_sheet
  delegating "windowWillClose:",                                        :to => :will_close
  delegating "windowWillMiniaturize:",                                  :to => :will_miniturize
  delegating "windowWillMove:",                                         :to => :will_move
  delegating "windowWillResize:toSize:",                                :to => :will_resize,              :parameters => [:toSize]
  delegating "windowWillReturnFieldEditor:toObject:",                   :to => :returning_field_editor,   :parameters => [:toObject]
  delegating "windowWillReturnUndoManager:",                            :to => :returning_undo_manager
  delegating "windowWillUseStandardFrame:defaultFrame:",                :to => :will_use_standard_frame,  :parameters => [:defaultFrame]

end