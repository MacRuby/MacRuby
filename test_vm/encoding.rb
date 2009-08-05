assert "US-ASCII", "File.open('../Rakefile', 'r:US-ASCII') {|f| puts f.read.encoding.name }"

assert ":ok", "puts ':ok'.encode('US-ASCII')"
