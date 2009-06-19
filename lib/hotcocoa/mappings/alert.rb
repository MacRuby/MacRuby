HotCocoa::Mappings.map :alert => :NSAlert do
  
  defaults :default => "OK", :alternate => nil, :other => nil, :info => "", :show => true
  
  def alloc_with_options(options)
    if options[:message]
      alert = NSAlert.alertWithMessageText options.delete(:message), 
        defaultButton:options.delete(:default), 
        alternateButton:options.delete(:alternate),
        otherButton:options.delete(:other),
        informativeTextWithFormat:options.delete(:info)
    end
  end
  
  custom_methods do
    
    def show
      runModal
    end
    
  end
  
  delegating "alertShowHelp:", :to => :show_help?
  
end
