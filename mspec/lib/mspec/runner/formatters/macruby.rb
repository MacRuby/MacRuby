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
    @stats.categories.each do |key, details|
      print "#{key}:\n" 
      print "  -> #{details[:failures]} failures, #{details[:errors]} errors (#{details[:expectations]} expectations, #{details[:examples]} examples, #{details[:skipped]} examples skipped, #{details[:files]} files) \n"
    end 
    
    print "\nSummary:\n"
    print "files: ",        @tally.counter.files,        "\n"
    print "examples: ",     @tally.counter.examples,     "\n"
    print "skipped examples: ", @stats.categories.inject(0){|sum, cat| sum += cat[:skipped]}, "\n"
    print "expectations: ", @tally.counter.expectations, "\n"
    print "failures: ",     @tally.counter.failures,     "\n"
    print "errors: ",       @tally.counter.errors,       "\n" 
    
    print "\nExceptions:\n"
    count = 0
    @exceptions.each do |exc|
      outcome = exc.failure? ? "FAILED" : "ERROR"
      print "\n#{count += 1})\n#{exc.description} #{outcome}\n"
      print exc.message, "\n"
      print exc.backtrace, "\n"
    end  
  end
end
