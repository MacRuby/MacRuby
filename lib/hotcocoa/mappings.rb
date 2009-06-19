module HotCocoa
  module Mappings
    
    def self.reload
      Dir.glob(File.join(File.dirname(__FILE__), "mappings", "*.rb")).each do |mapping|
        require mapping
      end
    end
    
    DefaultEmptyRect = [0,0,0,0]
    
    module TargetActionConvenience
      def on_action=(behavior)
        if target && (
          target.instance_variable_get("@action_behavior") || 
          target.instance_variable_get("@double_action_behavior"))
            object.instance_variable_set("@action_behavior", behavior)
            object = target
        else
          object = Object.new
          object.instance_variable_set("@action_behavior", behavior)
          setTarget(object)
        end
        def object.perform_action(sender)
          @action_behavior.call(sender)
        end
        setAction("perform_action:")
      end
     
      def on_action(&behavior)
        self.on_action = behavior
        self
      end
    end
    
    # TODO: Needs docs for all possible invocations and examples!
    def self.map(options, &block)
      framework = options.delete(:framework)
      mapped_name = options.keys.first
      mapped_value = options.values.first
      args = [mapped_name, mapped_value]
      
      if mapped_value.kind_of?(Class)
        add_mapping(*args, &block)
      else
        if framework.nil? || loaded_framework?(framework)
          add_constant_mapping(*args, &block)
        else
          on_framework(framework) do
            add_constant_mapping(*args, &block)
          end
        end
      end
    end
    
    # Registers +mapped_name+ as a Mapper#builder_method for the given
    # +mapped_value+. The +block+ is used as the Mapper#builder_method's body.
    def self.add_mapping(mapped_name, mapped_value, &block)
      m = Mapper.map_instances_of(mapped_value, mapped_name, &block)
      mappings[m.builder_method] = m
    end
    
    # Registers +mapped_name+ as a Mapper#builder_method for the given
    # +constant+ string which will be looked up. The +block+ is used as the
    # Mapper#builder_method's body.
    def self.add_constant_mapping(mapped_name, constant, &block)
      add_mapping(mapped_name, Object.full_const_get(constant), &block)
    end
    
    # Returns the Hash of mappings.
    def self.mappings
      @mappings ||= {}
    end
    
    # Registers a callback for after the specified framework has been loaded.
    def self.on_framework(name, &block)
      (frameworks[name.to_s] ||= []) << block
    end
    
    # Returns the Hash of mapped frameworks.
    def self.frameworks
      @frameworks ||= {}
    end
    
    # Returns the Set of loaded frameworks.
    def self.loaded_frameworks
      @loaded_frameworks ||= Set.new
    end
    
    # Registers a given framework as being loaded.
    def self.framework_loaded
      frameworks.keys.each do |key|
        if loaded_framework?(key)
          frameworks[key].each do |mapper|
            mapper.call
          end
          frameworks.delete(key)
        end
      end
    end
    
    # Returns whether or not the framework has been loaded yet.
    def self.loaded_framework?(name)
      NSBundle.allFrameworks.map {|bundle| bundle.bundlePath.split("/").last}.select {|framework| framework.split(".")[1] == 'framework'}.map {|framework| framework.split(".")[0]}.include?(name.to_s)
    end
    
  end
  
end
