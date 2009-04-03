#!/usr/bin/ruby
# Preliminary test suite for the MacRuby VM.
# Aimed at testing critical features of the VM.
#
# Please do not contribute tests that cover any higher level features here, 
# use the rubyspec directory instead.

$test_only = []
test_commands = []
ARGV.each do |arg|
  if md = /--ruby=(.*)/.match(arg)
    test_commands << md[1]
  else
    $test_only << arg
  end
end
if test_commands.empty?
  miniruby_path = File.join(Dir.pwd, 'miniruby')
  test_commands << "arch -i386 #{miniruby_path}"
  test_commands << "arch -x86_64 #{miniruby_path}" if system("arch -x86_64 #{miniruby_path} -e '' 2> /dev/null")
end
$test_archs = {}
test_commands.each do |command|
  if md = /\barch -([^\s]+)/.match(command)
    arch_name = md[1]
  else
    arch_name = 'default'
  end
  $test_archs[arch_name] ||= []
  $test_archs[arch_name] << command
end
$problems = []
$problems_count = 0
$assertions_count = 0

def assert(expectation, code, options={})
  return if options[:known_bug]
  if options[:archs]
    archs = $test_archs.select {|arch, command| options[:archs].include?(arch) }
  else
    archs = $test_archs
  end
  archs.each do |arch, commands|
    commands.each do |command|
      output = nil
      IO.popen(command, 'r+') do |io|
        io.puts(code)
        io.close_write
        output = io.read
      end
      result = if $? and $?.exitstatus == 0
        output.chomp == expectation ? '.' : 'F'
      else
        output = "ERROR CODE #{$?.exitstatus}"
        'E'
      end
      print result
      $stdout.flush
      if result != '.'
        $problems_count += 1
        new_problem = [[$problems_count], code, expectation, [arch], command, output]
        previous_problem = $problems.last
        if previous_problem and [1, 2, 5].all? {|i| previous_problem[i] == new_problem[i]}
          previous_problem[0] << $problems_count
          previous_problem[3] << arch
        else
          $problems << new_problem
        end
      end
      $assertions_count += 1
    end
  end
end

Dir.chdir("#{File.dirname(__FILE__)}/test_vm")
$tests = Dir.glob('*.rb').map {|filename| File.basename(filename, '.rb')}.sort

$test_only = $tests if $test_only.empty?
$test_only.each do |what|
  print "#{what} "
  $stdout.flush
  load "#{what}.rb"
  puts
end

at_exit do
  if $problems.empty?
    puts "Successfully passed all #{$assertions_count} assertions."
  else
    puts ''
    puts "#{$problems_count} assertion#{$problems_count > 1 ? 's' : ''} over #{$assertions_count} failed:"
    $problems.each do |ids, code, expectation, archs, command, output|
      puts ''
      puts "Problem#{ids.length > 1 ? 's' : ''} #{ids.join(', ')}:"
      puts "Code: #{code}"
      puts "Arch#{archs.length > 1 ? 's' : ''}: #{archs.join(', ')}"
      puts "Command: #{command}"
      puts "Expectation: #{expectation}"
      puts "Output: #{output}"
    end
    exit 1
  end
end
