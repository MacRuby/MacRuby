#!/usr/local/bin/macruby
require 'dispatch'
job = Dispatch::Job.new { Math.sqrt(10**100) }
@result = job.value
puts @result.to_int.to_s.size # => 50

job.value {|v| p v.to_int.to_s.size } # => 50 (eventually)
job.join
puts "All Done"

job.join { puts "All Done" }
job.add { Math.sqrt(2**64) }
@values = job.values 
puts @values.inspect # => [1.0E50, 4294967296.0]
job = Dispatch::Job.new {}
@hash = job.synchronize Hash.new
puts @hash.class # => Dispatch::Proxy

puts job.values.class # => Dispatch::Proxy

@hash[:foo] = :bar
puts @hash.to_s  # => "{:foo=>:bar}"


[64, 100].each do |n|
  job.add { @hash[n] = Math.sqrt(10**n) }
end
puts @hash.inspect # => {64 => 1.0E32, 100 => 1.0E50}

@hash.inspect { |s| p s } # => {64 => 1.0E32, 100 => 1.0E50}
delegate = @hash.__value__
puts delegate.class # => Hash

n = 42
job = Dispatch::Job.new { p n }
job.join # => 42

n = 0
job = Dispatch::Job.new { n = 42 }
job.join
p n # => 0 
n = 0
job = Dispatch::Job.new { n += 42 }
job.join
p n # => 0 
5.p_times { |i| puts 10**i } # => 1  100 1000 10 10000 

5.p_times(3) { |i| puts 10**i } # =>1000 10000 1 10 100 
%w(Mon Tue Wed Thu Fri).p_each { |day| puts day} # => Mon Wed Thu Tue Fri
%w(Mon Tue Wed Thu Fri).p_each(3) { |day| puts day} # =>  Thu Fri Mon Tue Wed
%w(Mon Tue Wed Thu Fri).p_each_with_index { |day, i | puts "#{i}:#{day}"} # => 0:Mon 2:Wed 3:Thu 1:Tue 4:Fri
%w(Mon Tue Wed Thu Fri).p_each_with_index(3) { |day, i | puts "#{i}:#{day}"} # => 3:Thu 4:Fri 0:Mon 1:Tue 2:Wed 
(0..4).p_map { |i| 10**i } # => [1, 1000, 10, 100, 10000]
(0..4).p_map(3) { |i| 10**i } # => [1000, 10000, 1, 10, 100]
(0..4).p_mapreduce(0) { |i| 10**i } # => 11111
(0..4).p_mapreduce([], :concat) { |i| [10**i] } # => [1, 1000, 10, 100, 10000]

(0..4).p_mapreduce([], :concat, 3) { |i| [10**i] } # => [1000, 10000, 1, 10, 100]
(0..4).p_find_all { |i| i.odd?} # => {3, 1}
(0..4).p_find_all(3) { |i| i.odd?} # => {3, 1}

(0..4).p_find_all { |i| i == 5 } # => nil
(0..4).p_find_all { |i| i.odd?} # => 1
(0..4).p_find_all(3) { |i| i.odd?} # => 3

timer = Dispatch::Source.periodic(0.9) { |src| puts src.data }
sleep 0.2 # => 1 1 ...

timer.suspend!
sleep 0.2
timer.resume!
sleep 0.2 # => 2 1 ...
timer.cancel!
@sum = 0
adder = Dispatch::Source.add { |s| @sum += s.data;  }
adder << 1 # => "add 1 -> 1"
adder.suspend!
adder << 3
adder << 5
adder.resume! # => "add 8 -> 9"
adder.cancel!
@mask = 0
masker = Dispatch::Source.or { |s| @mask |= s.data }
masker.suspend!
masker << 0b0011
masker << 0b1010
masker.resume!
puts  "%b" % @mask # => 1011
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
