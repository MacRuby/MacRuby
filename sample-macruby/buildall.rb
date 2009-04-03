require 'fileutils'
include FileUtils

def die(*x)
  STDERR.puts x
  exit 1
end

if ARGV.size != 1
  die "Usage: #{__FILE__} <output-directory>"
end

out_dir = ARGV.first
if !File.exist?(out_dir) or !File.directory?(out_dir)
  die "Given #{out_dir} doesn't exist or is not a directory"
end

tmp_dir = '/tmp/macruby_samples'
rm_rf tmp_dir
mkdir_p tmp_dir

succeeded, failed = [], []

Dir.glob('*/**/*.xcodeproj').each do |sampleDir|
  name = File.dirname(sampleDir)
  puts "Building #{name}..."
  Dir.chdir name do
    ary = system("xcodebuild SYMROOT=#{tmp_dir} >& /dev/null") ? succeeded : failed
    ary << name
  end
end

Dir.glob(File.join(tmp_dir, '**/*.app')).each do |app|
  cp_r app, out_dir
  app_name = File.basename(app)
  executable_name = app_name.gsub('.app', '')
  chmod 0755, File.join(out_dir, app_name, 'Contents', 'MacOS', executable_name)
end

[succeeded, failed].each { |a| a << 'None' if a.empty? }

puts "Successful to build: #{succeeded.join(', ')}"
puts "Failed to build: #{failed.join(', ')}"
