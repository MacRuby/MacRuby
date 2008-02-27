require 'rbconfig'
require 'fileutils'
include FileUtils

dest = "/Library/Application Support/Developer/3.0/Xcode"
mkdir_p(dest)

macruby_headers_dir1 = RbConfig::CONFIG['rubyhdrdir']
macruby_headers_dir2 = File.join(macruby_headers_dir1, RbConfig::CONFIG['sitearch']) 
macruby_library_dir = RbConfig::CONFIG['libdir']

path = File.dirname(__FILE__)
Dir.glob(File.join(path, '**', '*.in')).each do |file|
  text = File.read(file)
  text.gsub!(/PATH_TO_MACRUBY_HEADERS_1/, macruby_headers_dir1)
  text.gsub!(/PATH_TO_MACRUBY_HEADERS_2/, macruby_headers_dir2)
  text.gsub!(/PATH_TO_MACRUBY_LIBRARY/, macruby_library_dir)
  File.open(file[0..-4], 'w') { |io| io.write(text) }
end
cp_r(File.join(path, "Project Templates"), dest)
Dir.glob(File.join(path, '**', '.svn')).each { |x| rm_f(x) }
