namespace :spec do
  KNOWN_GOOD = %w{
    and
    array
    case
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
    while
  }
  
  KNOWN_PARTIALLY_GOOD = %w{
    alias
    block
    break
    catch
    class
    def
    eigenclass
    encoding
    ensure
    for
    loop
    magic_comment
    metaclass
    method
    next
    predefined
    private
    regexp
    rescue
    return
    throw
    undef
    variables
    yield
  }
  
  KNOWN_GOOD_AND_PARTIALLY_GOOD_FILES =
    FileList["spec/frozen/language/{#{(KNOWN_GOOD + KNOWN_PARTIALLY_GOOD).join(',')}}_spec.rb"]
  
  desc "Run all language known good spec files which should be fully green"
  task :green do
    files = FileList["spec/frozen/language/{#{KNOWN_GOOD.join(',')}}_spec.rb"]
    sh "./miniruby -v -I./mspec/lib -I./lib ./mspec/bin/mspec-run #{files.join(' ')}"
  end
  
  desc "Run continuous integration language examples (all known good examples)"
  task :ci do
    sh "./mspec/bin/mspec ci -B ./spec/frozen/macruby.mspec #{KNOWN_GOOD_AND_PARTIALLY_GOOD_FILES.join(' ')}"
  end
  
  desc "Run language examples that are known to fail"
  task :fails do
    sh "./mspec/bin/mspec run -g fails -B ./spec/frozen/macruby.mspec #{KNOWN_GOOD_AND_PARTIALLY_GOOD_FILES.join(' ')}"
  end
  
  namespace :list do
    desc "List all spec language spec files which do not load yet"
    task :not_loadable do
      puts((Dir['spec/frozen/language/*_spec.rb'] - KNOWN_GOOD_AND_PARTIALLY_GOOD_FILES).join("\n"))
    end
  end
  
  %w{ fails critical }.each do |tag|
    namespace :list do
      # We cheat by using the fact that currently the ruby.1.9.mspec script uses the macruby tags,
      # otherwise macruby fails halfway because apperantly the spec files are loaded when listing tagged specs...
      desc "List all specs that are tagged as `#{tag}'"
      task tag do
        sh "./mspec/bin/mspec tag --list #{tag} -B ./spec/frozen/ruby.1.9.mspec spec/frozen/language"
      end
    end
    
    namespace :"1.9" do
      desc "Run roxor language specs tagged `#{tag}' against Ruby 1.9 (use this to look for possible 1.8/1.9 incompatibility bugs)"
      task tag do
        sh "./mspec/bin/mspec run -g #{tag} -B ./spec/frozen/ruby.1.9.mspec #{KNOWN_GOOD_AND_PARTIALLY_GOOD_FILES.join(' ')}"
      end
    end
  end
end
