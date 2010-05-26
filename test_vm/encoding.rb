assert "US-ASCII", "File.open('../Rakefile', 'r:US-ASCII') {|f| puts f.read.encoding.name }"

assert ":ok", "puts ':ok'.encode('US-ASCII')"

assert "true", "p [].pack('U*').encoding.name == 'UTF-8'"