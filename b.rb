def bench(e, options)
  puts e
  ['./miniruby', 'ruby19'].each do |r|
    puts `#{r} -v`.strip
    line = File.exist?(e) ? "#{r} \"#{e}\"" : "#{r} -e \"#{e}\""
    n = options.include?('--no-rehearsal') ? 1 : 3
    n.times do 
      t = Time.now
      v = system(line) ? Time.now - t : nil
      puts '    ' + v.to_s
    end
  end
end
options = []
expressions = []
while e = ARGV.shift
  a = /^-/.match(e) ? options : expressions
  a << e
end
expressions.map! { |e| e.gsub(/"/, '\"') }
expressions.each { |e| bench(e, options) }
