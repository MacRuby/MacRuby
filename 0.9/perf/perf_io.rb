TEST_FILE = '/usr/share/dict/words'

perf_test('File.read') do
  File.read(TEST_FILE)
end

perf_test('File#each_line') do
  File.open(TEST_FILE).each_line {}
end
