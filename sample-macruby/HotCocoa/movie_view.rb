framework 'qtkit'

require 'hotcocoa'

include HotCocoa

application do |app|
  window :frame => [100, 100, 660, 308], :title => "HotCocoa!" do |win|
    mview = movie_view :frame => [10, 10, 640, 288], 
                       :movie => movie(:url => "http://movies.apple.com/movies/disney/wall-e/wall-e-tlr3_h.640.mov"),
                       :controller_buttons => [:back, :volume],
                       :fill_color => color(:name => :black)
    win << mview
  end
end

