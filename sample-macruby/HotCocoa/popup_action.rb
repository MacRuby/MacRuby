require 'hotcocoa'

include HotCocoa

sounds = Dir.glob("/System/Library/Sounds/*.aiff")

application :name => "Popup Action" do |app|
  window :frame => [200, 200, 300, 120], :title => "HotCocoa!" do |win|
    win << button(:frame => [10, 80, 100, 25], :title => "Sounds!") do |b|
      b.on_action do 
        sound(:file => sounds[rand(sounds.size)]).play
      end
    end
    win << popup(
      :frame => [120, 80, 110, 25], 
      :title => "Push Me!", 
      :items => ["One", "Two", "Three"],
      :on_action => Proc.new {|p| puts p.items.selected}
    )
  end
end


