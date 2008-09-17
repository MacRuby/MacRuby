HotCocoa::Mappings.map :toolbar_item => :NSToolbarItem do

  def init_with_options(toolbar_item, options)
    identifier = options.delete(:identifier)
    unless identifier
      label = options[:label]
      if label
        identifier = label.tr(' ', '_')
      else
        raise ArgumentError, ":identifier or :label required"
      end
    end
    toolbar_item.initWithItemIdentifier identifier
  end

  custom_methods do
    include TargetActionConvenience
  end

end
