require 'hotcocoa'

include HotCocoa

application :name => "Secure Text Field" do |app|
  window :frame => [200, 200, 300, 120], :title => "HotCocoa!" do |win|
    win << secure_text_field(:echo => true, :frame => [20,20,200,20])
  end
end
