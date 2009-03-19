namespace :spec do
  MSPEC = "./miniruby -v -I./mspec/lib -I./lib ./mspec/bin/mspec"
  
  desc "Run continuous integration examples for Ruby 1.9 including stdlib"
  task :ci do
    #sh "./mspec/bin/mspec ci -t ./miniruby -B spec/frozen/macruby.mspec"
    
    # TODO: Still fails at another require statment.
    # It seems to spawns yet another process which also needs the proper laod path.
    # Anyways load paths are currently broken on roxor. Will find out a tmp workaround tonight.
    sh "#{MSPEC}-ci -B spec/frozen/macruby.mspec"
  end
  
  desc "Run language examples"
  task :language do
    sh "#{MSPEC} spec/frozen/language/**/*_spec.rb"
  end
end