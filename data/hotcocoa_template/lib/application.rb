require 'hotcocoa'

include HotCocoa

# Replace the following code with your own hotcocoa code

application :name => "__APPLICATION_NAME__" do |app|
  window :frame => [100, 100, 500, 500], :title => "__APPLICATION_NAME__" do |win|
    win << label(:text => "Hello from HotCocoa", :layout => {:start => false})
    win.will_close { exit }
  end
end