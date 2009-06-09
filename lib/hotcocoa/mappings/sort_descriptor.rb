HotCocoa::Mappings.map :sort_descriptor => :NSSortDescriptor do

  defaults :ascending => true

  def init_with_options(sd, opts)
    if opts.has_key?(:selector)
      sd.initWithKey(opts.delete(:key), ascending:opts.delete(:ascending))
    else
      sd.initWithKey(opts.delete(:key), ascending:opts.delete(:ascending), selector:opts.delete(:selector))
    end
  end

end