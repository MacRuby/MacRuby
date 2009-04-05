namespace :execute do
  def command
    if ENV['e']
      ENV['e']
    else
      raise ArgumentError, 'To execute a command do: rake execute:all e="p :foo"'
    end
  end
  
  def run(bin)
    sh "#{bin} -e '#{command}'" rescue nil
  end
  
  desc "Run command (ENV['e']) with miniruby (MacRuby)"
  task :miniruby do
    run './miniruby'
  end
  
  desc "Run command (ENV['e']) with ruby (1.8)"
  task :ruby do
    run 'ruby'
  end
  
  desc "Run command (ENV['e']) with ruby19 (1.9)"
  task :ruby19 do
    run 'ruby19'
  end
  
  desc "Run command (ENV['e']) with ruby, ruby19, and miniruby"
  task :all => [:ruby, :ruby19, :miniruby]
end

desc "Run command (ENV['e']) with ruby, ruby19, and miniruby"
task :e => 'execute:all'