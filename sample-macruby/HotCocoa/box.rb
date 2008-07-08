begin
  require 'hotcocoa'
rescue LoadError => e
  $:.unshift "../../lib"
  require 'hotcocoa'
end

include HotCocoa

application do |app|
  window :frame => [200, 800, 300, 120], :title => "HotCocoa!" do |win|

    win.did_move do
      puts "Window moved to #{win.frame.inspect}!"
    end
    
    win.did_resize do
      puts "Window resized to #{win.frame.size.inspect}"
    end
    
    win << box(:title => "I am a in a box!", :frame => [0,10, 300, 110], :auto_resize => [:width, :height]) do |b|
      b << image_view(:frame => [10,10,60,60], :file => "rich.jpg")
    end
  end
end
