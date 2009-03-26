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
    retry
    string
    symbol
    unless
    until
    while
  }
  
  KNOWN_PARTIALLY_GOOD = %w{
    block
    break
    catch
    class
    def
    eigenclass
    encoding
    ensure
    loop
    magic_comment
    metaclass
    next
    precedence
    predefined
    private
    return
  }
  
  MSPEC_RUN = "./miniruby -v -I./mspec/lib -I./lib ./mspec/bin/mspec-run"
  
  desc "Run all spec files that should be fully green (known good)"
  task :green do
    sh "#{MSPEC_RUN} #{FileList["spec/frozen/language/{#{KNOWN_GOOD.join(',')}}_spec.rb"].join(' ')}"
  end
  
  desc "Run continuous integration language examples (all known good examples)"
  task :ci do
    files = FileList["spec/frozen/language/{#{(KNOWN_GOOD + KNOWN_PARTIALLY_GOOD).join(',')}}_spec.rb"]
    sh "./mspec/bin/mspec ci -B ./spec/frozen/macruby.mspec #{files.join(' ')}"
  end
  
  desc "Run language examples that are known to fail"
  task :fails do
    files = FileList["spec/frozen/language/*_spec.rb"]
    files -= files.grep(/\/(#{KNOWN_GOOD.join('|')})_spec\.rb$/)
    files.each do |spec|
      sh "./miniruby -v -I./mspec/lib -I./lib ./mspec/bin/mspec-run --format spec #{spec}" rescue nil
    end
  end
  
  desc "Run language examples"
  task :language do
    sh "./mspec/bin/mspec ci --format spec -B ./spec/frozen/macruby.mspec spec/frozen/language"
  end
  
  namespace :"1.9" do
    desc "Run Ruby 1.9 language examples"
    task :language do
      sh "./mspec/bin/mspec ci -B spec/frozen/ruby.1.9.mspec spec/frozen/language"
    end
  end
end
