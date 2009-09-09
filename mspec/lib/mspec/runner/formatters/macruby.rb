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
  
  def sum_skipped
    @stats.categories.inject(0){|sum, cat_info| sum += cat_info.last[:skipped].to_i}
  end

  def gen_rate(passed, skipped)
    "%0.2f" % [passed* (100 / (passed + skipped).to_f)]
  end

  def finish
    switch

    @stats.categories.each do |key, details|
      puts ""
      puts "#{key.capitalize}:"
      puts "  files: #{details[:files]}"
      puts "  examples: #{details[:examples]}"
      puts "  skipped examples: #{details[:skipped]}"
      puts "  expectations: #{details[:expectations]}"
      puts "  failures: #{details[:failures]}"
      puts "  errors: #{details[:errors]}"
      puts "  pass rate: #{gen_rate(details[:examples], details[:skipped])}%"
    end 

    puts "\nSummary:"
    puts "  files: #{@tally.counter.files}"
    puts "  examples: #{@tally.counter.examples}"
    puts "  skipped examples: #{sum_skipped}"
    puts "  expectations: #{@tally.counter.expectations}"
    puts "  failures: #{@tally.counter.failures}"
    puts "  errors: #{@tally.counter.errors}"
    puts "  pass rate: #{gen_rate(@tally.counter.examples, sum_skipped)}%"

    print "\nExceptions:\n" unless @exceptions.empty?
    count = 0
    @exceptions.each do |exc|
      outcome = exc.failure? ? "FAILED" : "ERROR"
      print "\n#{count += 1})\n#{exc.description} #{outcome}\n"
      print exc.message, "\n"
      print exc.backtrace, "\n"
    end  
  end
end
