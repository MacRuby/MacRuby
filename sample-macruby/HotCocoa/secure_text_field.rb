begin
  require 'hotcocoa'
rescue LoadError => e
  $:.unshift "../../lib"
  require 'hotcocoa'
end

include HotCocoa

application do |app|
  window :frame => [200, 200, 300, 120], :title => "HotCocoa!" do |win|
    win << secure_text_field(:echo => true, :frame => [20,20,200,20])
  end
end
