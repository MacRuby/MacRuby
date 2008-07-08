begin
  require 'hotcocoa'
rescue LoadError => e
  $:.unshift "../../lib"
  require 'hotcocoa'
end

include HotCocoa

application do |app|
  window :frame => [100, 100, 300, 80], :title => "HotCocoa!" do |win|
    win << label(:text => "This is a label", :font => font(:name => "Tahoma", :size => 40))
  end
end

