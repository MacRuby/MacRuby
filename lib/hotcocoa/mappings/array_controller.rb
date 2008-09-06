HotCocoa::Mappings.map :array_controller => :NSArrayController do

  def init_with_options(array_controller, options)
    result = array_controller.init
    if options.has_key?(:for)
      result.addObjects(options.delete(:for))
    end
    result
  end

  custom_methods do
    
    def avoids_empty_selection=(value)
      setAvoidsEmptySelection(value)
    end
    
    def avoids_empty_selection?
      avoidsEmptySelection
    end
    
    def preserves_selection=(value)
      setPreservesSelection(value)
    end
    
    def preserves_selection?
      preservesSelection
    end
    
    def rearrange_automatically=(value)
      setAutomaticallyRearrangesObjects(value)
    end
    
    def rearrange_automatically?
      automaticallyRearrangesObjects
    end
    
    def selects_inserted=(value)
      setSelectsInsertedObjects(value)
    end
    
    def selects_inserted?
      selectsInsertedObjects
    end
    
    def <<(object)
      addObject(object)
    end
    
    def [](index)
      arrangedObjects[index]
    end
    
    def each(&block)
      arrangedObjects.each(&block)
    end
    
    def selected
      selectedObjects.first
    end
    
    def sort_by=(sort_descriptors)
      sort_descriptors = [sort_descriptors] if sort_descriptors.kind_of?(Hash)
      descriptors = sort_descriptors.collect do |descriptor|
        selector = descriptor.delete(:selector)
        ascending = (descriptor.values.first == :ascending)
        if selector
          NSSortDescriptor.alloc.initWithKey(descriptor.keys.first.to_s, ascending: ascending, selector: selector)
        else
          NSSortDescriptor.alloc.initWithKey(descriptor.keys.first.to_s, ascending: ascending)
        end
      end
      setSortDescriptors(descriptors)
    end
    
  end

end

=begin
array :avoids_empty_selection => true, 
      :preserves_selection => false, 
      :selects_inserted => false, 
      :rearrange_automatically => true,
      :sort_by => {:name => :ascending}

array_controller = NSArrayController.new

array_controller.setAvoidsEmptySelection(false)
array_controller.setPreservesSelection(false)
array_controller.setSelectsInsertedObjects(false)
array_controller.setAutomaticallyRearrangesObjects(true)

array_controller.setSortDescriptors(NSArray.arrayWithObject(NSSortDescriptor.alloc.initWithKey("name", ascending: false)))
array_controller.addObject Icon.new("Rich", image(:file => "rich.jpg"))
=end