#!/usr/bin/ruby

def usage
  puts "ruby #{$0} [--rubies=...] [--iterations=n] [--displayTotal] [perf_suite, [perf_suite...]]"
  exit 0
end

usage if ARGV.size < 1

cwd = File.dirname(__FILE__)
perf_files = []
rubies = []
n_iterations = 3
totals = nil
ARGV.each do |arg|
  if arg.match(/--help/)
    usage
  elsif md = /--rubies=(.*)/.match(arg)
    rubies = md[1].split(/,/)
  elsif md = /--iterations=(.*)/.match(arg)
    n_iterations = md[1].to_i
    if n_iterations <= 0
      $stderr.puts "n_iterations must be greater than zero"
      exit 1
    end
  elsif arg == '-t' or arg == '--displayTotal'
    totals = {}
  else
    name, suite = arg.split(':', 2)
    perf_files << [File.join(cwd, "perf_#{name}.rb"), suite]
  end
end

if perf_files.empty?
  perf_files = Dir.glob(File.join(cwd, 'perf_*.rb')).map { |x| [x, nil] }
end

if rubies.empty?
  require 'rbconfig'
  rubies << File.join(RbConfig::CONFIG['bindir'],
    RbConfig::CONFIG['ruby_install_name'])
end

rubies_names = rubies.map { |x| `#{x} -v`.scan(/^\w+\s+[^\s]+/)[0] }

header_string = 'Name'.ljust(20)
rubies_names.each { |x| header_string += x.ljust(20) }
print header_string
puts '', '-' * header_string.length

booter = File.join(cwd, 'boot.rb')
perf_files.each do |file, suite|
  results = {}
  suite ||= ''
  rubies.each do |ruby| 
    output = `#{ruby} #{booter} #{n_iterations} #{file} #{suite}`.strip
    output.split(/\n/).each do |line|
      title, times = line.split(/:/)
      best = times.split(/,/).min
      results[title] ||= []
      results[title] << {:ruby => ruby, :best => best}
      if totals
        totals[ruby] = 0.0 if totals[ruby].nil?
        totals[ruby] += best.to_f unless best == 'ERROR'
      end
    end
  end
  prefix = File.basename(file).scan(/perf_(\w+)\.rb/)[0][0]
  results.each do |title, res|
    print "#{prefix}:#{title}".ljust(20)
    winner = nil
    if res.size > 1
      winner = res.reject { |a| a[:best] == 'ERROR' }.sort { |a, b|
        a[:best].to_f <=> b[:best].to_f
      }.first[:best]
    end
    res.each do |rb|
      best = rb[:best]
      s = best.ljust(20)
      if best == winner
        s = "\033[32m#{s}\033[0m" # green
      elsif best == 'ERROR'
        s = "\033[31m#{s}\033[0m" # red
      end
      print s
    end
    puts ''
  end
end

if totals
  puts '-' * header_string.length
  footer_string = 'Total'.ljust(20)
  rubies.each { |x| footer_string += totals[x].to_s.ljust(20) }
  puts footer_string
end
