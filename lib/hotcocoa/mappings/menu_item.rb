HotCocoa::Mappings.map :menu_item => :NSMenuItem do
  
  defaults :title => "", :key => "", :action => nil
  
  constant :modifiers, {
    :control    => NSControlKeyMask,
    :alt        => NSAlternateKeyMask,
    :command    => NSCommandKeyMask,
    :shift      => NSShiftKeyMask
  }
  
  def alloc_with_options(options)
    NSMenuItem.alloc.initWithTitle options.delete(:title), :action => options.delete(:action), :keyEquivalent => options.delete(:key)
  end
  
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
        
    def key
      keyEquivalent
    end
    
    def key=(value)
      setKeyEquivalent(value)
    end
    
    def modifiers=(value)
      setKeyEquivalentModifierMask(value)
    end
    
  end

end