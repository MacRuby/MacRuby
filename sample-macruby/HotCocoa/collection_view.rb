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
  
  attr_reader :image
  
  def collection_item=(item)
    image.bind "value",   toObject:item, withKeyPath:"representedObject.image", options:nil
    image.bind "toolTip", toObject:item, withKeyPath:"representedObject.name",  options:nil
  end
  
  def create_subviews
    @image = image_view :frame => [0,0,60,60]
    addSubview(image)
  end
  
  def hitTest(point)
    nil
  end
  
end

array_controller = NSArrayController.new
array_controller.setAvoidsEmptySelection(false)
array_controller.setPreservesSelection(false)
array_controller.setSelectsInsertedObjects(false)
array_controller.setAutomaticallyRearrangesObjects(true)
array_controller.setSortDescriptors(NSArray.arrayWithObject(NSSortDescriptor.alloc.initWithKey("name", ascending: false)))
array_controller.addObject Icon.new("Rich", image(:file => "rich.jpg"))

application do |app|
  window :frame => [100, 100, 500, 500], :title => "HotCocoa!" do |win|
    win << scroll_view(:frame => [10,10,480,470]) do |scroll|
      cv = collection_view :frame => [0,0,480,470], 
                                       :content => {array_controller => "arrangedObjects"}, 
                                       :selection_indexes => {array_controller => "selectionIndexes"},
                                       :item_view => MyIconView.create
      scroll << cv
    end
  end
end

