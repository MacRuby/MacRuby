module HotCocoa
  module Mappings
    
    def self.reload
      Dir.glob(File.join(File.dirname(__FILE__), "mappings", "*.rb")).each do |mapping|
        load mapping
      end
    end
    
    DefaultEmptyRect = [0,0,0,0]
    
    module TargetActionConvenience
      def on_action=(behavior)
        object = Object.new
        object.instance_variable_set("@behavior", behavior)
        def object.perform_action(sender)
          @behavior.call(sender)
        end
        setTarget(object)
        setAction("perform_action:")
      end
     
      def on_action(&behavior)
        self.on_action = behavior
        self
      end
    end
    
    def self.map(options, &block)
      framework = options.delete(:framework)
      mapped_name = options.keys.first
      mapped_value = options.values.first
      
      if mapped_value.kind_of?(Class)
        add_mapping(mapped_name, mapped_value, &block)
      else
        on_framework(framework) do
          constant = Object.full_const_get(mapped_value)
          add_mapping(mapped_name, constant, &block)
        end
      end
    end
    
    # Registers +mapped_name+ as a Mapper#builder_method for the given
    # +mapped_value+. The +block+ is used as the Mapper#builder_method's body.
    def self.add_mapping(mapped_name, mapped_value, &block)
      m = Mapper.map_instances_of(mapped_value, mapped_name, &block)
      mappings[m.builder_method] = m
    end
    
    def self.mappings
      @mappings ||= {}
    end
    
    def self.on_framework(name, &block)
      name = name.nil? ? nil : name.to_s.downcase
      if name.nil? or loaded_frameworks.include?(name)
        block.call
      else
        (frameworks[name] ||= []) << block
      end
    end
    
    def self.frameworks
      @frameworks ||= {}
    end
    
    def self.loaded_frameworks
      @loaded_frameworks ||= Set.new
    end
    
    def self.framework_loaded(name)
      name = name.to_s.downcase
      loaded_frameworks << name
      if frameworks[name]
        frameworks[name].each do |mapper|
          mapper.call
        end
      end
    end
    
  end
  
end
