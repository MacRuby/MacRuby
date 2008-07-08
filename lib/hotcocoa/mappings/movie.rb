HotCocoa::Mappings.map :movie => :QTMovie, :framework => :QTKit do

  def alloc_with_options(options)
    if options.has_key?(:file)
      QTMovie.movieWithFile(options.delete(:file), error:options.delete(:error))
    elsif options.has_key?(:url)
      QTMovie.movieWithURL(NSURL.alloc.initWithString(options.delete(:url)), error:options.delete(:error))
    else
      raise "Can only allocate a movie from a file or a url"
    end
  end

end
