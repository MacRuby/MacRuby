require 'hotcocoa'

include HotCocoa

application :name => "Table View" do |app|
  window :frame => [100, 100, 500, 500], :title => "HotCocoa!" do |win|
    people = table_view :frame => [10, 10, 480, 470], 
      :columns => [
        column(:id => :first_name, :text => "First"), 
        column(:id => :last_name, :text => "Last")
        ],
      :data => [
        {:first_name => "Richard", :last_name => "Kilmer"},
        {:first_name => "Chad",    :last_name => "Fowler"}
      ]
    win << split_view(:frame => [10, 10, 480, 470], :auto_resize => [:width, :height]) do |split|
      split << scroll_view(:frame => [10,10,480,470]) do |scroll|
        scroll << people
      end
      split << button(:title => "Test")
    end
  end
end

