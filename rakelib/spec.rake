namespace :spec do
  CI_DIRS = %w{
    spec/frozen/language
    spec/frozen/core/argf
    spec/frozen/core/array
    spec/frozen/core/basicobject
    spec/frozen/core/bignum
    spec/frozen/core/builtin_constants
    spec/frozen/core/class
    spec/frozen/core/comparable
    spec/frozen/core/dir
    spec/frozen/core/encoding
    spec/frozen/core/enumerable
    spec/frozen/core/env
    spec/frozen/core/exception
    spec/frozen/core/false
    spec/frozen/core/file
    spec/frozen/core/filetest
    spec/frozen/core/gc
    spec/frozen/core/hash
    spec/frozen/core/integer
    spec/frozen/core/io
    spec/frozen/core/kernel
    spec/frozen/core/matchdata
    spec/frozen/core/math
    spec/frozen/core/method
    spec/frozen/core/module
    spec/frozen/core/nil
    spec/frozen/core/numeric
    spec/frozen/core/object
    spec/frozen/core/range
    spec/frozen/core/regexp
    spec/frozen/core/signal
    spec/frozen/core/string
    spec/frozen/core/symbol
    spec/frozen/core/systemexit
    spec/frozen/core/time
    spec/frozen/core/true
    spec/frozen/core/unboundmethod
  }.join(' ')
  
  MACRUBY_MSPEC = "./spec/macruby.mspec"
  DEFAULT_OPTIONS = "-I./lib -B #{MACRUBY_MSPEC}"
  
  def mspec(type, options)
    sh "./mspec/bin/mspec #{type} #{DEFAULT_OPTIONS} #{ENV['opts']} #{options}"
  end
  
  desc "Run continuous integration language examples (all known good examples)"
  task :ci do
    mspec :ci, "./spec/macruby #{CI_DIRS}"
  end
  
  desc "Run continuous integration language examples (all known good examples) (32 bit mode)"
  task :ci32 do
    sh "/usr/bin/arch -arch i386 ./miniruby ./mspec/bin/mspec-ci #{DEFAULT_OPTIONS} #{CI_DIRS}"
  end
  
  desc "Run all MacRuby-only specs"
  task :macruby do
    mspec :ci, "./spec/macruby"
  end
  
  task :todo do
    p(Dir.glob('spec/frozen/core/*') - CI_DIRS.split(' '))
  end
  
  desc "Run language examples that are known to fail"
  task :fails do
    mspec :run, "-g fails #{CI_DIRS}"
  end
  
  namespace :fails do
    task :verbose do
      desc "Run language examples that are known to fail with spec and verbose output"
      task :fails do
        mspec :run, "-V -f s -g fails #{CI_DIRS}"
      end
    end
  end
  
  namespace :tag do
    desc "Removed fail tags for examples which actually pass. (FIXME)"
    task :remove do
      mspec :tag, "-g fails --del fails #{CI_DIRS}"
    end
    
    desc "Tags failing examples in spec/core, specify the class to tag with the env variable `class'"
    task :add do
      klass = ENV['class']
      puts "Tagging failing examples of class `#{klass}'"
      
      tag_base = "./spec/frozen/tags/macruby/core/#{klass}"
      mkdir_p tag_base
      
      Dir.glob("./spec/frozen/core/#{klass}/*_spec.rb").each do |spec_file|
        puts "Running spec: #{spec_file}"
        cmd = "./mspec/bin/mspec ci -I./lib -f s -B ./spec/macruby.mspec #{spec_file}"
        out = `#{cmd}`
        
        if out.match(/^1\)(.+?)(FAILED|ERROR)/m)
          failures = $1.strip.split("\n")
          
          tag_file = "#{tag_base}/#{spec_file.match(/\/(\w+)_spec\.rb$/)[1]}_tags.txt"
          puts "Writing tags file: #{tag_file}"
          
          File.open(tag_file, 'a+') do |f|
            f << "\n" unless f.read.empty?
            failures.each do |failure|
              f << "fails:#{failure}\n"
            end
          end
        end
      end
    end
  end
  
  %w{ fails critical }.each do |tag|
    namespace :list do
      # We cheat by using the fact that currently the ruby.1.9.mspec script uses the macruby tags,
      # otherwise macruby fails halfway because apperantly the spec files are loaded when listing tagged specs...
      desc "List all specs that are tagged as `#{tag}'"
      task tag do
        sh "./mspec/bin/mspec tag --list #{tag} -B ./spec/frozen/ruby.1.9.mspec #{CI_DIRS}"
      end
    end
    
    namespace :"1.9" do
      desc "Run roxor language specs tagged `#{tag}' against Ruby 1.9 (use this to look for possible 1.8/1.9 incompatibility bugs)"
      task tag do
        sh "./mspec/bin/mspec run -g #{tag} -B ./spec/frozen/ruby.1.9.mspec #{CI_DIRS}"
      end
    end
  end
end
