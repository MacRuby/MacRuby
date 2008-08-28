framework 'qtkit'

class MovieView

  def self.description 
    "Movies" 
  end
  
  def self.create
    layout_view :frame => [0, 0, 0, 0], :layout => {:expand => [:width, :height]}, :margin => 0, :spacing => 0 do |view|
      mview = movie_view :layout => {:expand =>  [:width, :height]},
                         :movie => movie(:url => "http://movies.apple.com/movies/disney/wall-e/wall-e-tlr3_h.640.mov"),
                         :controller_buttons => [:back, :volume],
                         :fill_color => color(:name => :black)
      view << mview
    end
  end

  DemoApplication.register(self)
  
end