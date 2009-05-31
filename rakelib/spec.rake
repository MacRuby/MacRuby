namespace :spec do
  KNOWN_GOOD = %w{
    and
    array
    case
    catch
    class
    class_variable
    defined
    else
    execution
    file
    hash
    if
    line
    module
    numbers
    not
    or
    order
    precedence
    redo
    retry
    string
    super
    symbol
    unless
    until
    yield
    while
  }
  
  KNOWN_GOOD_CORE_IO = %w{
    binmode
    closed
    constants
    each_byte
    fileno
    fsync
    flush
    getc
    io
    inspect
    initialize_copy
    putc
    readchar
    sync
    tell
    to_i
    to_io
    write
    initialize
  }
  
  KNOWN_GOOD_CORE_IO_FILES = FileList["spec/frozen/core/io/{#{KNOWN_GOOD_CORE_IO.join(',')}}_spec.rb"]
  
  CI_DIRS = %w{
    spec/frozen/language
    spec/frozen/core/array
    spec/frozen/core/basicobject
    spec/frozen/core/class
    spec/frozen/core/comparable
    spec/frozen/core/enumerable
    spec/frozen/core/exception
    spec/frozen/core/false
    spec/frozen/core/file
    spec/frozen/core/hash
    spec/frozen/core/math
    spec/frozen/core/method
    spec/frozen/core/nil
    spec/frozen/core/numeric
    spec/frozen/core/object
    spec/frozen/core/symbol
    spec/frozen/core/true
    spec/frozen/core/unboundmethod
  }.join(' ')
  
  MACRUBY_MSPEC = "./spec/macruby.mspec"
  
  desc "Run all language known good spec files which should be fully green (does not use tags)"
  task :green do
    files = FileList["spec/frozen/language/{#{KNOWN_GOOD.join(',')}}_spec.rb"]
    sh "./miniruby -v -I./mspec/lib -I./lib ./mspec/bin/mspec-run #{files.join(' ')}"
  end
  
  desc "Run all partially good language spec files which are not yet fully green (does not use tags)"
  task :partially_green do
    files = Dir["spec/frozen/language/*_spec.rb"] - FileList["spec/frozen/language/{#{KNOWN_GOOD.join(',')}}_spec.rb"]
    sh "./miniruby -v -I./mspec/lib -I./lib ./mspec/bin/mspec-run #{files.join(' ')}"
  end
  
  desc "Run continuous integration language examples (all known good examples)"
  task :ci do
    sh "./mspec/bin/mspec ci -B #{MACRUBY_MSPEC} spec/macruby #{CI_DIRS} #{KNOWN_GOOD_CORE_IO_FILES.join(' ')}"
  end
  
  desc "Run continuous integration language examples (all known good examples) (32 bit mode)"
  task :ci32 do
    sh "/usr/bin/arch -arch i386 ./miniruby ./mspec/bin/mspec-ci -B #{MACRUBY_MSPEC} #{CI_DIRS} #{KNOWN_GOOD_CORE_IO_FILES.join(' ')}"
  end
  
  desc "Run IO test with GDB enabled"
  task :gdbio do
    sh "gdb --args ./miniruby -v -I./mspec/lib -I./lib ./mspec/bin/mspec-run #{KNOWN_GOOD_CORE_IO_FILES.join(' ')}"
  end
  
  desc "Run the IO tests that pass"
  task :io do
    sh "./miniruby -v -I./mspec/lib -I./lib ./mspec/bin/mspec-run #{KNOWN_GOOD_CORE_IO_FILES.join(' ')}"
  end
  
  desc "Run all MacRuby-only specs"
  task :macruby do
    sh "./mspec/bin/mspec ci -B #{MACRUBY_MSPEC} ./spec/macruby"
  end
  
  desc "Run language examples that are known to fail"
  task :fails do
    sh "./mspec/bin/mspec run -V -f s -g fails -B #{MACRUBY_MSPEC} #{CI_DIRS}"
  end
  
  desc "Tags failing examples in spec/core, specify the class to tag with the env variable `class'"
  task :tag_failing do
    klass = ENV['class']
    puts "Tagging failing examples of class `#{klass}'"
    
    tag_base = "./spec/frozen/tags/macruby/core/#{klass}"
    mkdir_p tag_base
    
    Dir.glob("./spec/frozen/core/#{klass}/*_spec.rb").each do |spec_file|
      cmd = "./mspec/bin/mspec ci -f s -B ./spec/macruby.mspec #{spec_file}"
      out = `#{cmd}`
      
      if out.match(/^1\)(.+?)(FAILED|ERROR)/m)
        failures = $1.strip.split("\n")
        
        tag_file = "#{tag_base}/#{spec_file.match(/\/(\w+)_spec\.rb$/)[1]}_tags.txt"
        puts "Writing tags file: #{tag_file}"
        
        File.open(tag_file, 'a+') do |f|
          failures.each do |failure|
            f << "fails:#{failure}\n"
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
