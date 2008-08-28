require 'hotcocoa'

include HotCocoa

class Icon
  
  ib_outlet :name, :image
  
  def initialize(name, image)
    @name = name
    puts "crea of #{object_id} - #{@name}"
    @image = image
  end
  
  def image
    @image
  end
  
  def name
    puts "Name of #{object_id} - #{@name}"
    @name
  end
  
end

class MyIconView < NSView
  
  include HotCocoa::Behaviors
  
  def self.create
    view = alloc.initWithFrame([0,0,60,60])
    view.create_subviews
    view
  end
  
  attr_reader :icon
  
  def collection_item=(item)
    icon.bind "value",   toObject:item, withKeyPath:"representedObject.image", options:nil
    icon.bind "toolTip", toObject:item, withKeyPath:"representedObject.name",  options:nil
  end
  
  def create_subviews
    @icon = image_view :frame => [0,0,60,60]
    addSubview(icon)
  end
  
  def hitTest(point)
    nil
  end
  
end
list = (1..5).collect { |i| Icon.new("Rich #{i}", image(:file => "rich.jpg"))}
puts list.inspect
icons = array_controller  :for => list ,
                          :avoids_empty_selection => true, 
                          :preserves_selection => false, 
                          :selects_inserted => false, 
                          :rearrange_automatically => true, 
                          :sort_by => {:name => :ascending}
                          
puts icons.arrangedObjects.first.class
                          
application :name => "Collection View" do |app|
  window :frame => [100, 100, 500, 500], :title => "HotCocoa!" do |win|
    win << scroll_view(:layout => {:expand => [:width, :height]}) do |scroll|
      cv = collection_view :content => {icons => "arrangedObjects"}, 
                           :selection_indexes => {icons => "selectionIndexes"},
                           :item_view => MyIconView.create,
                           :map_bindings => true
      scroll << cv
    end
  end
end

