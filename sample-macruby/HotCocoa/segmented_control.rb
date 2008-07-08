require 'hotcocoa'

include HotCocoa

application do |app|
  window :frame => [200, 200, 500, 120], :title => "HotCocoa!" do |win|
    segment = segmented_control(
      :frame => [10,10,480,60], 
      :segments => [
        {:label => "Richard", :width => 120},
        {:label => "Laurent", :width => 120},
        {:label => "Chad",    :width => 120},
        {:label => "Marcel",  :width => 120}
      ]
    )
    segment.on_action do
      puts "Selected #{segment.selected_segment.label}"
    end
    win << segment
  end
end
