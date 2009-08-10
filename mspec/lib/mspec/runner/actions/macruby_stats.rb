class MacRubySpecStats
  attr_accessor :categories
  attr_reader :category, :subcategory, :file, :tag_files
  
  def initialize
    @categories = {}
    @tag_files  = []
  end 
  
  # return the current arborescence
  def push_file(path)
    category = parse_path(path)
    @categories[category] ||= {:files => 0, :examples => 0, :expectations => 0, :failures => 0, :errors => 0, :skipped => 0}  
    @categories[category][:files] += 1
    category
  end
  
  def push_skipped(category, amount)
    increase_stats(category, :skipped, amount)
  end
  
  # increases the amount of examples in the category
  def example!(category)
    increase_stats(category, :examples)
  end
  
  def expectation!(category) 
    increase_stats(category, :expectations)
  end
  
  def failure!(category)
    increase_stats(category, :failures) 
  end
  
  def error!(category)
    increase_stats(category, :errors) 
  end
  
  protected
  
  # Return the spec category
  def parse_path(path)
    /spec\/(frozen|macruby)\/(.+?)\//  =~ path
    $2 
  end
  
  def increase_stats(category, type, amount=1)
    @categories[category][type] += amount
  end
  
end


class MacRubyStatsAction
  
  attr_reader :current_category
  
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
    @current_category = @stats.push_file(MSpec.retrieve(:file))
    skilled_specs = MSpec.read_tags(['critical', 'fails']).size
    @stats.push_skipped(current_category, skilled_specs) if skilled_specs > 0
  end
  
  def example(state, block)
    @stats.example!(current_category)
  end
  
  def expectation(state)
    print "."
    @stats.expectation!(current_category)
  end
  
  def exception(exception)
    exception.failure? ? @stats.failure!(current_category) : @stats.error!(current_category)
  end 
  
  def categories
    @stats.categories
  end
  
  def tags
    @stats.tag_files
  end
  
end