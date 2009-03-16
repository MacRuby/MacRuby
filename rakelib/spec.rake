namespace :spec do
  desc "Run continuous integration examples for Ruby 1.9 including stdlib"
  task :ci do
    #sh "./mspec/bin/mspec ci -t ./miniruby -B spec/frozen/macruby.mspec"
    
    # TODO: Still fails at another require statment.
    # It seems to spawns yet another process which also needs the proper laod path.
    # Anyways load paths are currently broken on roxor. Will find out a tmp workaround tonight.
    sh "./miniruby -v -I./mspec/lib ./mspec/bin/mspec-ci -B spec/frozen/macruby.mspec"
  end
end