HotCocoa::Mappings.map :sort_descriptor => :NSSortDescriptor do

  defaults :ascending => true

  def init_with_options(sort_descriptor, opts)
    if opts.has_key?(:selector)
      sort_descriptor.initWithKey(opts.delete(:key), ascending:opts.delete(:ascending))
    else
      sort_descriptor.initWithKey(opts.delete(:key), ascending:opts.delete(:ascending), selector:opts.delete(:selector))
    end
  end

end