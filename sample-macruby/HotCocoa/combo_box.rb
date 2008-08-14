require 'hotcocoa'

include HotCocoa

class Person < Struct.new(:first, :last)
  def to_s
    "#{first} #{last}"
  end
end
people = [
  Person.new("Rich", "Kilmer"),
  Person.new("Chad", "Fowler"),
  Person.new("Tom", "Copeland")
]

application :name => "Combo Box" do |app|
  window :frame => [100, 100, 500, 500], :title => "HotCocoa!" do |win|
    win << combo_box(:frame => [10, 10, 100, 25], :data => people)
  end
end

