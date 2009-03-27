namespace :bench do

  desc "Run the regression performance suite"
  task :ci do
    sh "./miniruby -I./lib bench.rb"
  end

end
