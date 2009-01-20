AppConfig = HotCocoa::ApplicationBuilder::Configuration.new("config/build.yml")

task :deploy => [:clean] do
  HotCocoa::ApplicationBuilder.build(AppConfig, :deploy => true)
end

task :build do
  HotCocoa::ApplicationBuilder.build(AppConfig)
end

task :run => [:build] do
  `"./#{AppConfig.name}.app/Contents/MacOS/#{AppConfig.name.gsub(/ /, '')}"`
end

task :clean do
  `/bin/rm -rf "#{AppConfig.name}.app"`
end
