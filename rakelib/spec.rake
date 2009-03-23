namespace :spec do
  MSPEC_RUN = "./miniruby -v -I./mspec/lib -I./lib ./mspec/bin/mspec-run"
  
  KNOWN_GOOD = %w{ and or order until while module }
  
  desc "Run continuous integration language examples (known good)"
  task :ci do
    sh "#{MSPEC_RUN} #{FileList["spec/frozen/language/{#{KNOWN_GOOD.join(',')}}_spec.rb"].join(' ')}"
  end
  
  desc "Run language examples"
  task :language do
    sh "./mspec/bin/mspec ci -B ./spec/frozen/macruby.mspec spec/frozen/language/*_spec.rb"
  end
  
  namespace :workaround do
    desc "Run language examples with a workaround which uses mspec-run on each individual spec"
    task :language do
      Dir.glob('spec/frozen/language/**/*_spec.rb').each do |spec|
        sh "#{MSPEC_RUN} #{spec}" rescue nil
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
