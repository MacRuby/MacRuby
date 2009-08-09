require 'mspec/expectations/expectations'
require 'mspec/runner/formatters/dotted'
require 'mspec/runner/actions/macruby_stats'

class MacRubyFormatter < DottedFormatter
  def initialize(out=nil)
    @exception = @failure = false
    @exceptions = []
    @count = 0
    @out = $stdout

    if out.nil?
      @finish = $stdout
    else
      @finish = File.open out, "w"
    end
  end

  def switch
    @out = @finish
  end

  def after(state)
  end
  
  def register
    super
    (@stats = MacRubyStatsAction.new).register
  end

  def finish
    switch
     
    print "\n"
    @stats.categories.each do |category, subcategories|
      print "#{category}:\n"
      subcategories.each do |subcat, stats|
        print "  #{subcat}: #{stats[:failures]} failures, #{stats[:errors]} errors (#{stats[:examples]} examples, #{stats[:expectations]} expectations, #{stats[:files]} files) \n"
        
      end
    end
    print "\nSummary:\n"
    print "files: ",        @tally.counter.files,        "\n"
    print "examples: ",     @tally.counter.examples,     "\n"
    print "expectations: ", @tally.counter.expectations, "\n"
    print "failures: ",     @tally.counter.failures,     "\n"
    print "errors: ",       @tally.counter.errors,       "\n"   
  end
end
