perf_test('lit') do
  i = 0
  while i < 1000
    s = "#{i}"
    eval(s); eval(s); eval(s); eval(s); eval(s)
    eval(s); eval(s); eval(s); eval(s); eval(s)
    eval(s); eval(s); eval(s); eval(s); eval(s)
    eval(s); eval(s); eval(s); eval(s); eval(s)
    eval(s); eval(s); eval(s); eval(s); eval(s)
    eval(s); eval(s); eval(s); eval(s); eval(s)
    i += 1
  end
end
