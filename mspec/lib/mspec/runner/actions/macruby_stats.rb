class MacRubySpecStats
  attr_accessor :categories
  attr_reader :category, :subcategory, :file
  
  def initialize
    @categories = {}
  end 
  
  # return the current arborescence
  def push_file(path)
    cat, subcat, specfile = parse_path(path)
    @categories[cat] ||= {}
    @categories[cat][subcat] ||= {:files => 0, :examples => 0, :expectations => 0, :failures => 0, :errors => 0}
    @categories[cat][subcat][:files] += 1
    [cat, subcat, specfile] 
  end
  
  # increases the amount of examples in the category
  def example!(arborescence)
    increase_stats(arborescence, :examples)
  end
  
  def expectation!(arborescence) 
    increase_stats(arborescence, :expectations)
  end
  
  def failure!(arborescence)
    increase_stats(arborescence, :failures) 
  end
  
  def error!(arborescence)
    increase_stats(arborescence, :errors) 
  end
  
  protected
  
  def parse_path(path)
    # Ruby 1.9 only
    /.*frozen\/(?<category>.+?)\/(?<subcategory>.+)\/(?<file>.+_spec)\.rb/ =~ path
    [category, subcategory, file]
  end
  
  def increase_stats(arborescence, type)
    cat, subcat, specfile = arborescence
    @categories[cat][subcat][type] += 1
  end
  
end


class MacRubyStatsAction
  
  attr_reader :current_arborescence
  
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
    @current_arborescence = @stats.push_file(MSpec.retrieve(:file))
  end
  
  def example(state, block)
    @stats.example!(current_arborescence)
  end
  
  def expectation(state)
    print "."
    @stats.expectation!(current_arborescence)
  end
  
  def exception(exception)
    exception.failure? ? @stats.failure!(current_arborescence) : @stats.error!(current_arborescence)
  end 
  
  def categories
    @stats.categories
  end
  
end