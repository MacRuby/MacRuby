#!/usr/bin/env ruby
require 'pp'
LAST_YEAR=2009
NEXT_YEAR=2010
GREP="grep -il 'Copyright.*#{LAST_YEAR}.*Apple'"
REPLACE="-e '/Copyright/ s^\-2009, Apple^\-2010, Apple^' -e '/Copyright/ s^2009, Apple^2009\-2010, Apple^'"
BACKUP = "sedsave"

files = []
%w(rb c cpp h m).each do |ext|
  files += `find . -name "*.#{ext}" -print0 | xargs -0 #{GREP}`.split "\n"
end
files += `#{GREP} bin/*`.split "\n"
pp files
p "Found #{files.size} copyrights to update"
files.each do |file|
  p "Updating copyright of #{file}"
  `sed -i #{BACKUP} #{REPLACE} #{file}`
end
p "Removing backups"
`find . -name "*#{BACKUP}" -delete`
