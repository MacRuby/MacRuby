#!/usr/local/bin/macruby

require 'dispatch'	
job = Dispatch::Job.new { Math.sqrt(10**100) }
@result = job.value
puts "value (sync): #{@result} => 1.0e+50"

job.value {|v| puts "value (async): #{v.to_int.to_s.size} => 1.0e+50" } # (eventually)
job.join
puts "join done (sync)"

job.join { puts "join done (async)" }
job.add { Math.sqrt(2**64) }
job.value {|b| puts "value (async): #{b} => 4294967296.0" }
@values = job.values
puts "values: #{@values.inspect} => [1.0E50]"
job.join
puts "values: #{@values.inspect} => [1.0E50, 4294967296.0]"
job = Dispatch::Job.new {}
@hash = job.synchronize Hash.new
puts "synchronize: #{@hash.class} => Dispatch::Proxy"

puts "values: #{job.values.class} => Dispatch::Proxy"

@hash[:foo] = :bar
puts "proxy: #{@hash} => {:foo=>:bar}"
@hash.delete :foo


[64, 100].each do |n|
	job.add { @hash[n] = Math.sqrt(10**n) }
end
job.join
puts "proxy: #{@hash} => {64 => 1.0E32, 100 => 1.0E50}"

@hash.inspect { |s| puts "inspect: #{s} => {64 => 1.0E32, 100 => 1.0E50}" }
delegate = @hash.__value__
puts "\n__value__: #{delegate.class} => Hash"

n = 42
job = Dispatch::Job.new { puts "n (during): #{n} => 42" }
job.join

n = 0
job = Dispatch::Job.new { n = 21 }
job.join
puts "n (after): #{n} => 0?!?"
n = 0
job = Dispatch::Job.new { n += 84 }
job.join
puts "n (+=): #{n} => 0?!?"
5.times { |i| print "#{10**i}\t" }
puts "times"

5.p_times { |i| print "#{10**i}\t" }
puts "p_times"

5.p_times(3) { |i| print "#{10**i}\t" }
puts "p_times(3)"
DAYS=%w(Mon Tue Wed Thu Fri)
DAYS.each { |day| print "#{day}\t"}
puts "each"
DAYS.p_each { |day| print "#{day}\t"}
puts "p_each"
DAYS.p_each(3) { |day| print "#{day}\t"}
puts "p_each(3)"
DAYS.each_with_index { |day, i | print "#{i}:#{day}\t"}
puts "each_with_index"
DAYS.p_each_with_index { |day, i | print "#{i}:#{day}\t"}
puts "p_each_with_index"
DAYS.p_each_with_index(3) { |day, i | print "#{i}:#{day}\t"}
puts "p_each_with_index(3)"
print (0..4).map { |i| "#{10**i}\t" }.join
puts "map"

print (0..4).p_map { |i| "#{10**i}\t" }.join
puts "p_map"
print (0..4).p_map(3) { |i| "#{10**i}\t" }.join
puts "p_map(3) [sometimes fails!?!]"
mr = (0..4).p_mapreduce(0) { |i| 10**i }
puts "p_mapreduce: #{mr} => 11111"
mr = (0..4).p_mapreduce([], :concat) { |i| [10**i] }
puts "p_mapreduce(:concat): #{mr} => [1, 1000, 10, 100, 10000]"

mr = (0..4).p_mapreduce([], :concat, 3) { |i| [10**i] }
puts "p_mapreduce(3): #{mr} => [1000, 10000, 1, 10, 100]"
puts "find_all | p_find_all | p_find_all(3)"
puts (0..4).find_all { |i| i.odd? }.inspect
puts (0..4).p_find_all { |i| i.odd? }.inspect
puts (0..4).p_find_all(3) { |i| i.odd? }.inspect

puts "find | p_find | p_find(3)"
puts (0..4).find { |i| i == 5 } # => nil
puts (0..4).p_find { |i| i == 5 } # => nil
puts (0..4).p_find(3) { |i| i == 5 } # => nil
puts "#{(0..4).find { |i| i.odd? }} => 1"
puts "#{(0..4).p_find { |i| i.odd? }} => 1?"
puts "#{(0..4).p_find(3) { |i| i.odd? }} => 3?"
puts q = Dispatch::Queue.for("my_object")
q.sync {}

timer = Dispatch::Source.periodic(0.4) { |src| puts "Dispatch::Source.periodic: #{src.data}" }
sleep 1 # => 1 1 ...

timer.suspend!
puts "suspend!"
sleep 1
timer.resume!
puts "resume!"
sleep 1 # => 1 2 1 ...
timer.cancel!
puts "cancel!"
@sum = 0
adder = Dispatch::Source.add(q) { |s| puts "Dispatch::Source.add: #{s.data} (#{@sum += s.data})" }
adder << 1
q.sync {}
puts "sum: #{@sum} => 1"
adder.suspend!
adder << 3
adder << 5
q.sync {}
puts "sum: #{@sum} => 1"
adder.resume!
q.sync {}
puts "sum: #{@sum} => 9"
adder.cancel!
@mask = 0
masker = Dispatch::Source.or(q) { |s| puts "Dispatch::Source.or: #{s.data.to_s(2)} (#{(@mask |= s.data).to_s(2)})"}
masker << 0b0001
q.sync {}
puts "mask: #{@mask.to_s(2)} => 1"
masker.suspend!
masker << 0b0011
masker << 0b1010
puts "mask: #{@mask.to_s(2)} => 1"
masker.resume!
q.sync {}
puts "mask: #{@mask.to_s(2)} => 1011"
masker.cancel!
@event = 0
mask = Dispatch::Source::PROC_EXIT | Dispatch::Source::PROC_SIGNAL
proc_src = Dispatch::Source.process($$, mask, q) do |s|
	puts "Dispatch::Source.process: #{s.data} (#{@event |= s.data})"
end


@events = []
mask2 = [:exit, :fork, :exec, :signal]
proc_src2 = Dispatch::Source.process($$, mask2, q) do |s|
	@events += Dispatch::Source.data2events(s.data)
	puts "Dispatch::Source.process: #{Dispatch::Source.data2events(s.data)} (#{@events})"
end
sig_usr1 = Signal.list["USR1"]
Signal.trap(sig_usr1, "IGNORE")
Process.kill(sig_usr1, $$)
Signal.trap(sig_usr1, "DEFAULT")
q.sync {}
puts "@event: #{(result = @event & mask).to_s(2)} => 1000000000000000000000000000 (Dispatch::Source::PROC_SIGNAL)"
proc_src.cancel!
puts "@events: #{(result2 = @events & mask2)} => [:signal]"
proc_src2.cancel!
puts "event2num: #{Dispatch::Source.event2num(result2[0])} => #{result}"
puts "data2events: #{Dispatch::Source.data2events(result)} => #{result2}"
@signals = 0
sig_usr2 = Signal.list["USR2"]
signal = Dispatch::Source.signal(sig_usr2, q) do |s|
	puts "Dispatch::Source.signal: #{s.data} (#{@signals += s.data})"
end
signal.suspend!
Signal.trap(sig_usr2, "IGNORE")
3.times { Process.kill(sig_usr2, $$) }
Signal.trap(sig_usr2, "DEFAULT")
puts "signals: #{@signals} => 0"
signal.resume!
q.sync {}
puts "signals: #{@signals} => 3"
signal.cancel!
@fevent = 0
@msg = "#{$$}-#{Time.now.to_s.gsub(' ','_')}"
puts "msg: #{@msg}"
filename = "/tmp/dispatch-#{@msg}"
puts "filename: #{filename}"
file = File.open(filename, "w")
fmask = Dispatch::Source::VNODE_DELETE | Dispatch::Source::VNODE_WRITE
file_src = Dispatch::Source.file(file.fileno, fmask, q) do |s|
	puts "Dispatch::Source.file: #{s.data.to_s(2)} (#{(@fevent |= s.data).to_s(2)})"
end
file.print @msg
file.flush
file.close
q.sync {}
puts "fevent: #{@fevent & fmask} => #{Dispatch::Source::VNODE_WRITE} (Dispatch::Source::VNODE_WRITE)"
File.delete(filename)
q.sync {}
puts "fevent: #{@fevent} => #{fmask} (Dispatch::Source::VNODE_DELETE | Dispatch::Source::VNODE_WRITE)"
file_src.cancel!

@fevent2 = []
file = File.open(filename, "w")
fmask2 = %w(delete write)
file_src2 = Dispatch::Source.file(file, fmask2, q) do |s|
	@fevent2 += Dispatch::Source.data2events(s.data)
	puts "Dispatch::Source.file: #{Dispatch::Source.data2events(s.data)} (#{@fevent2})"
end
file.print @msg
file.flush
q.sync {}
puts "fevent2: #{@fevent2} => [:write]"
file_src2.cancel!

file = File.open(filename, "r")
@input = ""
reader = Dispatch::Source.read(file, q) do |s|
	@input << file.read(s.data)
	puts "Dispatch::Source.read: #{s.data}: #{@input}"
end
while (@input.size < @msg.size) do; end
q.sync {}
puts "input: #{@input} => msg" # => e.g., 74323-2010-07-07_15:23:10_-0700
reader.cancel!
file = File.open(filename, "w")
@message = @msg.dup
writer = Dispatch::Source.write(file, q) do |s|
	if @message.size > 0 then
		char = @message[0..0]
		file.write(char)
		rest = @message[1..-1]
		puts "Dispatch::Source.write: #{char}|#{rest}"
		@message = rest
	end
end
while (@message.size > 0) do; end
puts "output: #{File.read(filename)} => msg" # e.g., 74323-2010-07-07_15:23:10_-0700
	
