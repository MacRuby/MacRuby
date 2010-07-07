#!/usr/local/bin/macruby

require 'dispatch'	
job = Dispatch::Job.new { Math.sqrt(10**100) }
@result = job.value
puts "#{@result.to_int.to_s.size} => 50"

job.value {|v| puts "#{v.to_int.to_s.size} => 50" } # (eventually)
job.join
puts "join done (sync)"

job.join { puts "join done (async)" }
job.add { Math.sqrt(2**64) }
job.value {|b| puts "#{b} => 4294967296.0" }
@values = job.values
puts "#{@values.inspect} => [1.0E50]"
job.join
puts "#{@values.inspect} => [1.0E50, 4294967296.0]"
job = Dispatch::Job.new {}
@hash = job.synchronize Hash.new
puts "#{@hash.class} => Dispatch::Proxy"

puts "#{job.values.class} => Dispatch::Proxy"

@hash[:foo] = :bar
puts "#{@hash} => {:foo=>:bar}"
@hash.delete :foo


[64, 100].each do |n|
	job.add { @hash[n] = Math.sqrt(10**n) }
end
job.join
puts "#{@hash} => {64 => 1.0E32, 100 => 1.0E50}"

@hash.inspect { |s| puts "#{s} => {64 => 1.0E32, 100 => 1.0E50}" }
delegate = @hash.__value__
puts "\n#{delegate.class} => Hash"

n = 42
job = Dispatch::Job.new { puts "#{n} => 42" }
job.join

n = 0
job = Dispatch::Job.new { n = 42 }
job.join
puts "#{n} => 0 != 42"
n = 0
job = Dispatch::Job.new { n += 42 }
job.join
puts "#{n} => 0 != 42"
5.times { |i| print "#{10**i}\t" }
puts "done times"

5.p_times { |i| print "#{10**i}\t" }
puts "done p_times"

5.p_times(3) { |i| print "#{10**i}\t" }
puts "done p_times(3)"
DAYS=%w(Mon Tue Wed Thu Fri)
DAYS.each { |day| print "#{day}\t"}
puts "done each"
DAYS.p_each { |day| print "#{day}\t"}
puts "done p_each"
DAYS.p_each(3) { |day| print "#{day}\t"}
puts "done p_each(3)"
DAYS.each_with_index { |day, i | print "#{i}:#{day}\t"}
puts "done each_with_index"
DAYS.p_each_with_index { |day, i | print "#{i}:#{day}\t"}
puts "done p_each_with_index"
DAYS.p_each_with_index(3) { |day, i | print "#{i}:#{day}\t"}
puts "done p_each_with_index(3)"
print (0..4).map { |i| "#{10**i}\t" }.join
puts "done map"

print (0..4).p_map { |i| "#{10**i}\t" }.join
puts "done p_map"
print (0..4).p_map(3) { |i| "#{10**i}\t" }.join
puts "done p_map(3) [sometimes fails!?!]"
mr = (0..4).p_mapreduce(0) { |i| 10**i }
puts "#{mr} => 11111"
mr = (0..4).p_mapreduce([], :concat) { |i| [10**i] }
puts "#{mr} => [1, 1000, 10, 100, 10000]"

mr = (0..4).p_mapreduce([], :concat, 3) { |i| [10**i] }
puts "#{mr} => [1000, 10000, 1, 10, 100]"
puts (0..4).find_all { |i| i.odd? }.inspect
puts (0..4).p_find_all { |i| i.odd? }.inspect
puts (0..4).p_find_all(3) { |i| i.odd? }.inspect

puts (0..4).find { |i| i == 5 } # => nil
puts (0..4).p_find { |i| i == 5 } # => nil
puts "#{(0..4).find { |i| i.odd? }} => 1"
puts "#{(0..4).p_find { |i| i.odd? }} => 1?"
puts "#{(0..4).p_find(3) { |i| i.odd? }} => 3?"

timer = Dispatch::Source.periodic(0.4) { |src| puts "periodic: #{src.data}" }
sleep 1 # => 1 1 ...

timer.suspend!
puts "suspend!"
sleep 1
timer.resume!
puts "resume!"
sleep 1 # => 2 1 ...
timer.cancel!
puts "cancel!"
@sum = 0
adder = Dispatch::Source.add { |s| puts "add #{s.data} => #{@sum += s.data}" }
adder << 1
adder.suspend!
adder << 3
adder << 5
adder.resume!
adder.cancel!
@mask = 0
masker = Dispatch::Source.or { |s| puts "or #{s.data.to_s(2)} => #{(@mask |= s.data).to_s(2)}"}
masker.suspend!
masker << 0b0011
masker << 0b1010
masker.resume!
masker.cancel!
@event = 0
mask = Dispatch::Source::PROC_EXIT | Dispatch::Source::PROC_SIGNAL
proc_src = Dispatch::Source.process($$, mask) do |s|
	@event |= s.data
end


@events = []
mask2 = [:exit, :fork, :exec, :signal]
proc_src2 = Dispatch::Source.process($$, mask2) do |s|
	@events << Dispatch::Source.data2events(s.data)
end
sig_usr1 = Signal.list["USR1"]
Signal.trap(sig_usr1, "IGNORE")
Process.kill(sig_usr1, $$)
Signal.trap(sig_usr1, "DEFAULT")
result = "%b" % (@event & mask) # => 1000000000000000000000000000 # Dispatch::Source::PROC_SIGNAL
proc_src.cancel!
result2 = (@events & mask2) # => [:signal]
proc_src2.cancel!
puts result == Dispatch::Source#event2num(result2[0]) # => true
puts result2[0] == Dispatch::Source#num2event(result) # => true
@count = 0
sig_usr2 = Signal.list["USR2"]
signal = Dispatch::Source.signal(sig_usr2) do |s|
	@count += s.data
end
signal.suspend!
Signal.trap(sig_usr2, "IGNORE")
3.times { Process.kill(sig_usr2, $$) }
Signal.trap(sig_usr2, "DEFAULT")
signal.resume!
puts @count # => 3
signal.cancel!
@fevent = 0
@msg = "#{$$}-#{Time.now.to_s.gsub(' ','_')}"
filename = "/tmp/dispatch-#{@msg}"
file = File.open(filename, "w")
fmask = Dispatch::Source::VNODE_DELETE | Dispatch::Source::VNODE_WRITE
file_src = Dispatch::Source.file(file.fileno, fmask) do |s|
	@fevent |= s.data
end
file.puts @msg
file.flush
file.close
puts @fevent & fmask # => Dispatch::Source::VNODE_WRITE
File.delete(filename)
puts @fevent == fmask # => true
file_src.cancel!

@fevent2 = []
file = File.open(filename, "w")
fmask2 = %w(delete write)
file_src2 = Dispatch::Source.file(file, fmask2) do |s|
	@fevent2 << Dispatch::Source.data2events(s.data)
end
file.puts @msg
file.flush
puts @fevent2 & fmask2 # => [:write]
file_src2.cancel!

file = File.open(filename, "r")
@result = ""
reader = Dispatch::Source.read(file) do |s|
	@result << @file.read(s.data)
end
while (@result.size < @msg.size) do; end
puts @result # => e.g., 489-Wed_Mar_24_15:59:00_-0700_2010
reader.cancel!
file = File.open(filename, "w")
@message = @msg
writer = Dispatch::Source.write(file) do |s|
	if @message.size > 0 then
		char = @message[0..0]
		@file.write(char)
		@message = @message[1..-1]
	end
end
while (@message.size > 0) do; end
result = File.read(filename)
puts result # => e.g., 489-Wed_Mar_24_15:59:00_-0700_2010
	
