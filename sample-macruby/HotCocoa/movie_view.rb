framework 'qtkit'

begin
  require 'hotcocoa'
rescue LoadError => e
  $:.unshift "../../lib"
  require 'hotcocoa'
end

include HotCocoa

application do |app|
  window :frame => [100, 100, 500, 500], :title => "HotCocoa!" do |win|
    mview = movie_view :frame => [10, 10, 480, 480], 
                       :movie => movie(:file => "table_view.mov"),
                       :controller_buttons => [:back, :volume],
                       :fill_color => color(:name => :black)
    win << mview
  end
end

