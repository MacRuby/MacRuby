module HotCocoa::Mappings
  
  class Mapper
    
    attr_reader :control_class, :builder_method, :control_module

    def self.map_class(klass)
      new(klass).include_in_class
    end
    
    def self.map_instances_of(klass, builder_method, &block)
      new(klass).map_method(builder_method, &block)
    end
    
    def initialize(klass)
      @control_class = klass
    end
    
    def include_in_class
      @extension_method = :include
      customize(@control_class)
    end
    
    def map_method(builder_method, &block)
      @extension_method = :extend
      @builder_method = builder_method
      mod = (class << self; self; end)
      mod.extend MappingMethods
      mod.module_eval &block
      @control_module = mod
      inst = self
      HotCocoa.send(:define_method, builder_method) do |*args, &control_block|
        map = (args.length == 1 ? args[0] : args[1]) || {}
        guid = args.length == 1 ? nil : args[0]
        map = inst.remap_constants(map)
        default_empty_rect_used = (map[:frame].__id__ == DefaultEmptyRect.__id__)
        control = inst.respond_to?(:init_with_options) ? inst.init_with_options(inst.control_class.alloc, map) : inst.alloc_with_options(map)
        Views[guid] = control if guid
        inst.customize(control)
        map.each do |key, value|
          if control.respond_to?(key) && value == true
            control.send("#{key}")
          else
            eval "control.#{key}= value"
          end
        end
        if default_empty_rect_used
          control.sizeToFit if control.respondsToSelector(:sizeToFit) == 1
        end
        if control_block
          if inst.respond_to?(:handle_block)
            inst.handle_block(control, &control_block)
          else
            control_block.call(control)
          end
        end
        control
      end
      self
    end
    
    def inherited_constants
      constants = {}
      each_control_ancestor do |ancestor|
        constants = constants.merge(ancestor.control_module.constants_map)
      end
      constants
    end
    
    def inherited_delegate_methods
      delegate_methods = {}
      each_control_ancestor do |ancestor|
        delegate_methods = delegate_methods.merge(ancestor.control_module.delegate_map)
      end
      delegate_methods
    end
    
    def inherited_custom_methods
      methods = []
      each_control_ancestor do |ancestor|
        methods << ancestor.control_module.custom_methods if ancestor.control_module.custom_methods
      end
      methods
    end
    
    def each_control_ancestor
      control_class.ancestors.reverse.each do |ancestor|
        Mappings.mappings.values.each do |mapper|
          yield mapper if mapper.control_class == ancestor
        end
      end
    end
    
    def customize(control)
      inherited_custom_methods.each do |custom_methods|
        control.send(@extension_method, custom_methods)
      end
      decorate_with_delegate_methods(control)
    end

    def decorate_with_delegate_methods(control)
      delegate_module = Module.new
      inherited_delegate_methods.each do |delegate_method, mapping|
        parameters = mapping[:parameters] ? ", "+mapping[:parameters].map {|param| %{"#{param}"} }.join(",") : ""
        delegate_module.module_eval %{
          def #{mapping[:to]}(&block)
            raise "Must pass in a block to use this delegate method" unless block_given?
            @_delegate_builder ||= HotCocoa::DelegateBuilder.new(self)
            @_delegate_builder.add_delegated_method(block, "#{delegate_method}" #{parameters})
          end
        }
      end
      control.send(@extension_method, delegate_module)
    end

    def remap_constants(tags)
      constants = inherited_constants
      if control_module.defaults
        control_module.defaults.each do |key, value| 
          tags[key] = value unless tags.has_key?(key)
        end
      end
      result = {}
      tags.each do |tag, value|
        if constants[tag]
          result[tag] = value.kind_of?(Array) ? value.inject(0) {|a, i| a|constants[tag][i]} : constants[tag][value]
        else
          result[tag] = value
        end
      end
      result
    end

  end
end