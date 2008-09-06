HotCocoa::Mappings.map :control => :NSControl do
  
  custom_methods do
    
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
    
    def text=(text)
      setStringValue(text)
    end

    def to_i
      intValue
    end

    def to_f
      doubleValue
    end
    
    def to_s
      stringValue
    end

  end

end
