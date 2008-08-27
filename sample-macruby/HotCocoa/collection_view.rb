require 'hotcocoa'

include HotCocoa

class Icon
  attr_accessor :name, :image
  def initialize(name, image)
    @name = name
    @image = image
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

icons = array_controller  :for => (1..100).collect { |i| Icon.new("Rich #{i}", image(:file => "rich.jpg")) },
                          :avoids_empty_selection => true, 
                          :preserves_selection => false, 
                          :selects_inserted => false, 
                          :rearrange_automatically => true, 
                          :sort_by => {:name => :ascending}
                          
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

