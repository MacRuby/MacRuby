HotCocoa::Mappings.map :status_bar => :NSStatusBar do

  def alloc_with_options(options)
    NSStatusBar.systemStatusBar
  end

end
