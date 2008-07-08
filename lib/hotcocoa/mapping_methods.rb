module HotCocoa
  
  module MappingMethods
    
    def defaults(defaults=nil)
      if defaults
        @defaults = defaults
      else
        @defaults
      end
    end
    
    def constant(name, constants)
      constants_map[name] = constants
    end
    
    def constants_map
      @constants_map ||= {}
    end
    
    def custom_methods(&block)
      if block
        @custom_methods = Module.new
        @custom_methods.module_eval(&block)
      else
        @custom_methods
      end
    end
    
    def delegating(name, options)
      delegate_map[name] = options
    end
    
    def delegate_map
      @delegate_map ||= {}
    end
    
  end
  
end