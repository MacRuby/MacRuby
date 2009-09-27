namespace :spec do
  MACRUBY_MSPEC = "./spec/macruby.mspec"
  DEFAULT_OPTIONS = "-B #{MACRUBY_MSPEC}"
  
  def mspec(type, options)
    sh "./mspec/bin/mspec #{type} #{DEFAULT_OPTIONS} #{ENV['opts']} #{options}"
  end
  
  desc "Run all continuous integration examples (all known good examples)"
  task :ci do
    mspec :ci, ":full"
  end
  
  namespace :ci do
     desc "Run all continuous integration examples and report the stats (all known good examples)"
     task :stats do
       mspec :ci, ":full -f macruby"
     end
  end
  
  desc "Run continuous integration language examples (all known good examples) (32 bit mode)"
  task :ci32 do
    sh "/usr/bin/arch -arch i386 ./miniruby ./mspec/bin/mspec-ci #{DEFAULT_OPTIONS} :full"
  end
  
  desc "Run all MacRuby-only specs"
  task :macruby do
    mspec :ci, ":macruby"
  end

  desc "Run all Language-only specs"
  task :language do
    mspec :ci, ":language"
  end

  desc "Run all Core-only specs"
  task :core do
    mspec :ci, ":core"
  end

  desc "Run all Library-only specs"
  task :library do
    mspec :ci, ":library"
  end
  
  desc "Run language examples that are known to fail"
  task :fails do
    mspec :run, "-g fails :full"
  end
  
  namespace :fails do
    task :verbose do
      desc "Run language examples that are known to fail with spec and verbose output"
      task :fails do
        mspec :run, "-V -f s -g fails :full"
      end
    end
  end
  
  namespace :tag do
    desc "Removed fail tags for examples which actually pass."
    task :remove do
      mspec :tag, "-g fails --del fails :full"
    end
    
    desc "Add fails tags for examples which fail."
    task :add do
      mspec :tag, "-G critical -G fails :full"
    end
  end
  
  %w{ fails critical }.each do |tag|
    namespace :list do
      # We cheat by using the fact that currently the ruby.1.9.mspec script uses the macruby tags,
      # otherwise macruby fails halfway because apperantly the spec files are loaded when listing tagged specs...
      desc "List all specs that are tagged as `#{tag}'"
      task tag do
        sh "./mspec/bin/mspec tag --list #{tag} -B ./spec/frozen/ruby.1.9.mspec :full"
      end
    end
    
    namespace :"1.9" do
      desc "Run roxor language specs tagged `#{tag}' against Ruby 1.9 (use this to look for possible 1.8/1.9 incompatibility bugs)"
      task tag do
        sh "./mspec/bin/mspec run -g #{tag} -B ./spec/frozen/ruby.1.9.mspec :full"
      end
    end
  end
end
