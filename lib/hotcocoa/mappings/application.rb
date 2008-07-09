HotCocoa::Mappings.map :application => :NSApplication do
  
  def alloc_with_options(options)
    NSApplication.sharedApplication
  end
  
  def handle_block(application, &block)
    block.call(application)
    application.run
  end
  
  custom_methods do
    
    def name=(name)
      @name = name
    end
    
    def name
      @name
    end
    
  end
  
end