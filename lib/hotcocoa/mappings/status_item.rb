HotCocoa::Mappings.map :status_item => :NSStatusItem do

  defaults :length => NSVariableStatusItemLength

  def alloc_with_options(options)
    status_bar.statusItemWithLength options.delete(:length)
  end

end
