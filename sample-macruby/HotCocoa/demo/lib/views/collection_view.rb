class CollectionView

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

  def self.description 
    "Collection View" 
  end
  
  def self.icon_image
    image(:named => 'rich.jpg') || image(:file => File.expand_path(File.join(File.dirname(__FILE__), "..", "..", "resources", "rich.jpg")))
  end
  
  def self.create
    @icons = array_controller  :for => (1..100).collect { |i| Icon.new("Rich #{i}", icon_image) },
                              :avoids_empty_selection => true, 
                              :preserves_selection => false, 
                              :selects_inserted => false, 
                              :rearrange_automatically => true, 
                              :sort_by => {:name => :ascending}
    
    layout_view :frame => [0, 0, 0, 0], :layout => {:expand => [:width, :height]}, :margin => 0, :spacing => 0 do |view|
      view << scroll_view(:layout => {:expand => [:width, :height]}) do |scroll|
        cv = collection_view :content => {@icons => "arrangedObjects"}, 
                             :selection_indexes => {@icons => "selectionIndexes"},
                             :item_view => MyIconView.create,
                             :map_bindings => true
        scroll << cv
      end
    end
  end

  DemoApplication.register(self)
  
end
