AppConfig = ApplicationBuilder::Configuration.new("config/build.yml")

task :deploy => [:clean] do
  ApplicationBuilder.build(AppConfig, :deploy => true)
end

task :build do
  ApplicationBuilder.build(AppConfig)
end

task :run => [:build] do
  `/usr/bin/open "#{AppConfig.name}.app"`
end

task :clean do
  `/bin/rm -rf "#{AppConfig.name}.app"`
end
