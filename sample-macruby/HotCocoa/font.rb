require 'hotcocoa'

include HotCocoa

application do |app|
  window :frame => [200, 200, 300, 120], :title => "HotCocoa!" do |win|
    win << box(
      :title => "Very Big Font!", 
      :frame => [0,10, 300, 110], 
      :auto_resize => [:width, :height],
      :title_font => font(:system => 30)
    )
  end
end