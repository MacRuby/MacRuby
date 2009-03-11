HotCocoa::Mappings.map :toolbar_item => :NSToolbarItem do
  
  defaults :priority => :standard
  
  constant :priority, {
    :standard => NSToolbarItemVisibilityPriorityStandard,
    :low      => NSToolbarItemVisibilityPriorityLow,
    :high     => NSToolbarItemVisibilityPriorityHigh,
    :user     => NSToolbarItemVisibilityPriorityUser
  }

  def init_with_options(toolbar_item, options)
    if !options.has_key?(:label) && !options.has_key?(:identifier)
      raise ArgumentError, ":identifier or :label required" 
    end
    label = options[:label]
    toolbar_item.initWithItemIdentifier(options.delete(:identifier) || label.tr(' ', '_'))
    toolbar_item.paletteLabel = label if label
    toolbar_item
  end

  custom_methods do
    
    include HotCocoa::Mappings::TargetActionConvenience
    
    def priority=(value)
      setVisibilityPriority(value)
    end
    
    def priority
      visibilityPriority
    end
    
  end

end
