framework 'webkit'

require 'hotcocoa'

include HotCocoa

application :name => "Web View" do |app|
  window :frame => [100, 100, 500, 500], :title => "HotCocoa!" do |win|
    web = web_view :frame => [10, 10, 480, 470], :url => "http://www.ruby-lang.org"
    win << web
  end
end
