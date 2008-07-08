framework 'webkit'

begin
  require 'hotcocoa'
rescue LoadError => e
  $:.unshift "../../lib"
  require 'hotcocoa'
end

include HotCocoa

application do |app|
  window :frame => [100, 100, 500, 500], :title => "HotCocoa!" do |win|
    web = web_view :frame => [10, 10, 480, 470], :url => "http://www.ruby-lang.org"
    win << web
  end
end

