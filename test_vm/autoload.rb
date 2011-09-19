assert ':ok', %{
  File.unlink('zzz.rb') if File.file?('zzz.rb')
  instance_eval do
    autoload :ZZZ, './zzz.rb'
    begin
      ZZZ
    rescue LoadError
      p :ok
    end
  end
}

assert ':ok', %{
  open('zzz.rb', 'w') {|f| f.puts '' }
  instance_eval do
    autoload :ZZZ, './zzz.rb'
    begin
      ZZZ
    rescue NameError
      p :ok
    end
  end
}

assert ':ok', %{
  open('zzz.rb', 'w') {|f| f.puts 'class ZZZ; def self.ok; :ok;end;end'}
  instance_eval do
    autoload :ZZZ, './zzz.rb'
    p ZZZ.ok
  end
}

assert ':ok', %{
  open("zzz.rb", "w") {|f| f.puts "class ZZZ; def self.ok;:ok;end;end"}
  autoload :ZZZ, "./zzz.rb"
  p ZZZ.ok
}

assert ':ok', %{
  open("zzz.rb", "w") {|f| f.puts "class ZZZ; def self.ok;:ok;end;end"}
  autoload :ZZZ, "./zzz.rb"
  require "./zzz.rb"
  p ZZZ.ok
}

assert ':ok', %{
  open("zzz.rb", "w") {|f| f.puts "class ZZZ; def self.ok;:ok;end;end"}
  autoload :ZZZ, "zzz.rb"
  require "zzz.rb"
  p ZZZ.ok
}

assert '"okok"', %{
  open("zzz.rb", "w") {|f| f.puts "class ZZZ; def self.ok;:ok;end;end"}
  autoload :ZZZ, "./zzz.rb"
  t1 = Thread.new {ZZZ.ok}
  t2 = Thread.new {ZZZ.ok}
  p [t1.value, t2.value].join
}, :known_bug => true
