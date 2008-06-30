#!/usr/bin/env macruby

module Kernel
  class << self
    alias_method :__framework_before_rubycocoa_layer, :framework
    def framework(f)
      $LOADING_FRAMEWORK = true
      __framework_before_rubycocoa_layer(f)
      $LOADING_FRAMEWORK = false
    end
  end
end

class NSObject
  class << self
    alias_method :ib_outlets, :ib_outlet
    
    alias_method :__method_added_before_rubycocoa_layer, :method_added
    def method_added(mname)
      unless $LOADING_FRAMEWORK
        mname_str = mname.to_s
        unless mname_str =~ /^__|\s/
          parts = mname_str.split('_')
          if parts.length > 1 and parts.length == instance_method(mname).arity
            class_eval { alias_method (parts.join(':') << ':').to_sym, mname }
            return
          end
        end
      end
      __method_added_before_rubycocoa_layer(mname)
    end
  end
  
  def objc_send(*args)
    if args.length > 1
      selector, new_args = '', []
      (args.length / 2).times do
        selector << "#{args.shift}:"
        new_args << args.shift
      end
      send(selector, *new_args)
    else
      send(args.first)
    end
  end
  
  alias_method :__method_missing_before_rubycocoa_layer, :method_missing
  def method_missing(mname, *args, &block)
    if (parts = mname.to_s.split('_')).length > 1
       if parts.first == 'super'
         selector = args.empty? ? parts.last : parts[1..-1].join(':') << ':'
         if self.class.superclass.instance_methods.include?(selector.to_sym)
	   return __super_objc_send__(selector, *args)
         end
       end
      
      selector = parts.join(':') << ':'
      if respond_to?(selector) || respondsToSelector(selector) == 1
        eval "def #{mname}(*args); send('#{selector}', *args); end"
        return send(selector, *args)
      end
    end
    # FIXME: For some reason calling super or the original implementation
    # causes a stack level too deep execption. Is this a problem?
    #__method_missing_before_rubycocoa_layer(mname, *args, &block)
    
    raise NoMethodError, "undefined method `#{mname}' for #{inspect}:#{self.class}"
  end
end

module OSX
  class << self
    def require_framework(framework)
      Kernel.framework(framework)
    end
    
    def method_missing(mname, *args)
      if Kernel.respond_to? mname
        module_eval "def #{mname}(*args); Kernel.send(:#{mname}, *args); end"
        Kernel.send(mname, *args)
      else
        super
      end
    end
    
    def const_missing(constant)
      Object.const_get(constant)
    rescue NameError
      super
    end
  end
end
include OSX
