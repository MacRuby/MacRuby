class ButtonsView

  def self.description 
    "Button Views" 
  end
  
  def self.create
    layout_view :mode => :horizontal, :frame => [0, 0, 0, 0], :layout => {:expand => [:width, :height]} do |view|
      view.margin = 0
      view << button(:title => "hello", :layout => {:expand => :width, :start => false})
    end
  end

  DemoApplication.register(self)
  
end
