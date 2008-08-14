HotCocoa::Mappings.map :application => :NSApplication do
  
  def alloc_with_options(options)
    NSApplication.sharedApplication
  end
  
  def handle_block(application, &block)
    block.call(application)
    unless application.menu
      begin
        require 'lib/menu'
        application.menu = application_menu(application)
      rescue LoadError => e
        puts "No menu specified"
      end
    end
    application.run
  end
  
  custom_methods do
    
    def name=(name)
      @name = name
    end
    
    def name
      @name
    end
    
    def menu=(menu)
      setMainMenu(menu)
    end
    
    def menu
      mainMenu
    end
    
  end
  
end