begin
  require 'hotcocoa'
rescue LoadError => e
  $:.unshift "../../lib"
  require 'hotcocoa'
end

include HotCocoa

application do |app|
  window :frame => [200, 200, 300, 120], :title => "HotCocoa!"  do |win|
    win << button(:title => "Push Me!", :on_action => Proc.new {puts "Ouch"})
  end
end
