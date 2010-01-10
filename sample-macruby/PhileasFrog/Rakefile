require 'rake'

desc "Use MacRuby AOT compilation."
task :compile do
  files = ['*.rb', 'build/*/*.app/Contents/Resources/*.rb']
  files.map { |file| Dir.glob(File.join(File.dirname(__FILE__), file)) }.flatten.each do |path|
    `macrubyc -C #{path}`
  end
end