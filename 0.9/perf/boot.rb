if ARGV.size < 2 or ARGV.size > 3
  $stderr.puts "Usage: #{__FILE__} <n-iterations> <file.rb> [suite]"
  exit 1
end
N = ARGV[0].to_i
file = ARGV[1]
@suite = ARGV[2]
@perf_tests = []
def perf_test(name, &b)
  if @suite == nil or @suite == name
    @perf_tests << [name, b]
  end
end

load(file)

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
