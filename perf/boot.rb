@perf_tests = []
def perf_test(name, &b)
  @perf_tests << [name, b]  
end

if ARGV.size != 2
  $stderr.puts "Usage: #{__FILE__} <n-iterations> <file.rb>"
  exit 1
end
N = ARGV[0].to_i
load(ARGV[1])

@perf_tests.each do |name, proc|
  times = []
  N.times do
    ts = Time.now
    begin
      proc.call
      res = Time.now - ts
      times << ("%1.6f" % res)
    rescue
      times = ['ERROR']
      break
    end
  end
  puts "#{name}:#{times.join(",")}"
end
