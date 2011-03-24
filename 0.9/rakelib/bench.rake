namespace :bench do
  rubies = ENV['rubies']
  desc "Run the regression performance suite"
  task :ci do
    rubies = './miniruby' unless rubies
    sh "/usr/bin/ruby perf/run.rb --rubies=#{rubies}"
  end
end
