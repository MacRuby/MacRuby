HotCocoa::Mappings.map :application => :NSApplication do
  
  def alloc_with_options(options)
    NSApplication.sharedApplication
  end
  
  def handle_block(application, &block)
    application.load_application_menu
    block.call(application)
    application.run
  end
  
  custom_methods do
    
    def load_application_menu
      begin
        require 'lib/menu'
        o = Object.new
        o.extend HotCocoa
        setMainMenu(o.application_menu)
      rescue LoadError => e
      end
    end
    
    def name=(name)
      @name = name
    end
    
    def name
      @name
    end
    
    def menu=(menu)
      setMainMenu(menu)
    end
    
    def menu(path=nil)
      if path
        find_menu(mainMenu, path)
      else
        mainMenu
      end
    end
    
    def on_hide(menu)
      hide(menu)
    end
    
    def on_about(menu)
      orderFrontStandardAboutPanel(menu)
    end
    
    def on_hide_others(menu)
      hideOtherApplications(menu)
    end
    
    def on_show_all(menu)
      unhideAllApplications(menu)
    end
    
    def on_quit(menu)
      terminate(menu)
    end
    
    private
    
      def find_menu(menu, path)
        key = path.keys.first
        value = path.values.first
        menu = menu[key]
        if value.kind_of?(Array)
          find_menu(menu, value.first)
        else
          menu[value]
        end
      end
    
  end
  
end