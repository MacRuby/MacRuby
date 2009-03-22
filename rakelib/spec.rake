namespace :spec do
  MSPEC = "./miniruby -v -I./mspec/lib -I./lib ./mspec/bin/mspec"
  
  desc "Run continuous integration examples for Ruby 1.9 including stdlib"
  task :ci do
    #sh "./mspec/bin/mspec ci -t ./miniruby -B spec/frozen/macruby.mspec"
    
    # TODO: Still fails at another require statment.
    # It seems to spawns yet another process which also needs the proper laod path.
    # Anyways load paths are currently broken on roxor. Will find out a tmp workaround tonight.
    sh "#{MSPEC} ci -B spec/frozen/macruby.mspec spec/frozen/language"
  end
  
  desc "Run language examples"
  task :language do
    sh "./mspec/bin/mspec ci -B ./spec/frozen/macruby.mspec spec/frozen/language/*_spec.rb"
  end
  
  namespace :workaround do
    desc "Run language examples with a workaround which uses mspec-run on each individual spec"
    task :language do
      Dir.glob('spec/frozen/language/**/*_spec.rb').each do |spec|
        sh "#{MSPEC}-run #{spec}"
      end
    end
  end
  
  namespace :"1.9" do
    desc "Run Ruby 1.9 language examples"
    task :language do
      sh "./mspec/bin/mspec ci -B spec/frozen/ruby.1.9.mspec spec/frozen/language"
    end
  end
end