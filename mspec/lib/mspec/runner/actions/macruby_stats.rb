class MacRubySpecStats
  attr_accessor :categories
  attr_reader :category, :subcategory, :file
  
  def initialize
    @categories = {}
  end 
  
  def push_file(path)
    cat, subcat, specfile = parse_path(path)
    @categories[cat] ||= {}
    @categories[cat][subcat] ||= {:files => 0, :examples => 0, :expectations => 0, :failures => 0, :errors => 0}
    @categories[cat][subcat][:files] += 1
  end
  
  # increases the amount of examples in the category
  def example!(path)
    increase_stats(path, :examples)
  end
  
  def expectation!(path) 
    increase_stats(path, :expectations)
  end
  
  def failure!(path)
    increase_stats(path, :failures) 
  end
  
  def error!(path)
    increase_stats(path, :errors) 
  end
  
  protected
  
  def parse_path(path)
    path =~ /frozen\/(.+?)\/(.+)\/(.+_spec)\.rb/
    [$1, $2, $3]
  end
  
  def increase_stats(path, type)
    cat, subcat, specfile = parse_path(path)
    @categories[cat][subcat][type] += 1
  end
  
end


class MacRubyStatsAction
  
  def initialize  
    @stats = MacRubySpecStats.new
  end

  def register
    MSpec.register :load,        self
    MSpec.register :exception,   self
    MSpec.register :example,     self
    MSpec.register :expectation, self    
  end

  def unregister
    MSpec.unregister :load,        self
    MSpec.unregister :exception,   self
    MSpec.unregister :example,     self
    MSpec.unregister :expectation, self 
  end
  
  def load
    @stats.push_file MSpec.retrieve(:file)
  end
  
  def example(state, block)
    @stats.example! MSpec.retrieve(:file)
  end
  
  def expectation(state)
    print "."
    @stats.expectation! MSpec.retrieve(:file)
  end
  
  def exception(exception)
    exception.failure? ? @stats.failure!(MSpec.retrieve(:file)) : @stats.error!(MSpec.retrieve(:file))
  end 
  
  def categories
    @stats.categories
  end
  
end