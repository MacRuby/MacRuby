class ButtonsView

  def self.description 
    "Buttons" 
  end
  
  def self.create
    action = Proc.new {
      alert :message => "This is an alert!", :info => "This is a little more info!"
    }
    
    layout_view :frame => [0, 0, 0, 0], :layout => {:expand => [:width, :height]} do |view|
      view << button(:title => "Rounded", :bezel => :rounded, :layout => {:expand => :width, :start => false}, :on_action => action)
      view << button(:title => "Regular Square", :bezel => :regular_square, :layout => {:expand => :width, :start => false}, :on_action => action)
      view << button(:title => "Thick Square", :bezel => :thick_square, :layout => {:expand => :width, :start => false}, :on_action => action)
      view << button(:title => "Thicker Square", :bezel => :thicker_square, :layout => {:expand => :width, :start => false}, :on_action => action)
      #view << button(:title => "", :bezel => :disclosure, :layout => {:expand => :width, :start => false}, :on_action => action)
      view << button(:title => "Shadowless Square", :bezel => :shadowless_square, :layout => {:expand => :width, :start => false}, :on_action => action)
      #view << button(:title => "", :bezel => :circular, :layout => {:expand => :width, :start => false}, :on_action => action)
      view << button(:title => "Textured Square", :bezel => :textured_square, :layout => {:expand => :width, :start => false}, :on_action => action)
      #view << button(:title => "Help Button", :bezel => :help_button, :layout => {:expand => :width, :start => false}, :on_action => action)
      view << button(:title => "Small Square", :bezel => :small_square, :layout => {:expand => :width, :start => false}, :on_action => action)
      view << button(:title => "Textured Rounded", :bezel => :textured_rounded, :layout => {:expand => :width, :start => false}, :on_action => action)
      view << button(:title => "Round Rect", :bezel => :round_rect, :layout => {:expand => :width, :start => false}, :on_action => action)
      view << button(:title => "Recessed", :bezel => :recessed, :layout => {:expand => :width, :start => false}, :on_action => action)
      #view << button(:title => "", :bezel => :rounded_disclosure, :layout => {:expand => :width, :start => false}, :on_action => action)
    end
  end
  
  DemoApplication.register(self)
  
end
