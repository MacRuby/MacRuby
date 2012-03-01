str_ascii =<<EOS
Alice was beginning to get very tired of sitting by her sister on the
bank, and of having nothing to do: once or twice she had peeped into the
book her sister was reading, but it had no pictures or conversations in
it, 'and what is the use of a book,' thought Alice 'without pictures or
conversation?'
EOS

perf_test('new') do
  i = 0
  str = str_ascii.dup
  while i < 50_000
    String.new(str)
    i += 1
  end
end

perf_test('""') do
  i = 0
  str = str_ascii.dup
  while i < 50_000
    "{#{str_ascii}"
    i += 1
  end
end

perf_test('dup') do
  i = 0
  str = str_ascii.dup
  while i < 50_000
    str.dup
    i += 1
  end
end

perf_test('clone') do
  i = 0
  str = str_ascii.dup
  while i < 50_000
    str.clone
    i += 1
  end
end

perf_test('*') do
  i = 0
  str = str_ascii.dup
  while i < 500
    str * 1_000
    i += 1
  end
end

perf_test('+') do
  i = 0
  str1 = str_ascii.dup
  str2 = "0123456789"
  while i < 50_000
    str1 + str2
    i += 1
  end
end

perf_test('<<') do
  i = 0
  str1 = str_ascii.dup
  str2 = "0123456789"
  while i < 50_000
    str1 << str2
    i += 1
  end
end

perf_test('chomp!') do
  i = 0
  str = str_ascii.dup
  while i < 500
    str.chomp
    i += 1
  end
end

perf_test('[]') do
  i = 0
  str = str_ascii.dup
  while i < 50_000
    str[1..5]
    i += 1
  end
end

perf_test('[]=') do
  i = 0
  str = str_ascii.dup
  size = str.size
  while i < 50_000
    str[i%size] = "" << (i%255)
    i += 1
  end
end

perf_test('strip!') do
  i = 0
  str = str_ascii.dup
  while i < 50_000
    str.strip!
    i += 1
  end
end

perf_test('scan') do
  i = 0
  str = str_ascii.dup
  while i < 5_000
    str.scan(/\w+/)
    i += 1
  end
end


perf_test('split') do
  i = 0
  str = str_ascii.dup
  while i < 50_000
    str.split('\n')
    i += 1
  end
end

perf_test('gsub') do
  i = 0
  str = str_ascii.dup
  while i < 50_000
    str.gsub(/Alice/, "Rabbit")
    i += 1
  end
end


perf_test('reverse!') do
  i = 0
  str = str_ascii.dup
  while i < 50_000
    str.reverse!
    i += 1
  end
end


perf_test('=~') do
  i = 0
  str = str_ascii.dup
  while i < 50_000
    str =~ /Alice/
    i += 1
  end
end
