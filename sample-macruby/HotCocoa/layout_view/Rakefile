task :default => [:run]

task :build do
  require 'hotcocoa/application_builder'
  ApplicationBuilder.build :file => "config/build.yml"
end

task :run => [:build] do
  require 'yaml'
  app_name = YAML.load(File.read("config/build.yml"))[:name]
  `open "#{app_name}.app"`
end

task :clean do
  require 'yaml'
  app_name = YAML.load(File.read("config/build.yml"))[:name]
  `rm -rf "#{app_name}.app"`
end
