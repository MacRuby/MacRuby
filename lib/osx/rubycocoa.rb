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
  
  def to_ruby
    case self 
    when NSDate
      to_time
    when NSNumber
      integer? ? to_i : to_f
    when NSAttributedString
      string
    else
      self
    end
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

class NSData
  def rubyString
    NSString.alloc.initWithData(self, encoding:NSASCIIStringEncoding).mutableCopy
  end
end

class NSUserDefaults
  def [](key)
    objectForKey(key)
  end

  def []=(key, obj)
    setObject(obj, forKey:key)
  end

  def delete(key)
    removeObjectForKey(key)
  end
end

class NSIndexSet
  def to_a
    result = []
    index = firstIndex
    until index == NSNotFound
      result << index
      index = indexGreaterThanIndex(index)
    end
    return result
  end
    
  def inspect
    "#<#{self.class} #{self.to_a.inspect}>"
  end
end

class NSNumber
  def to_i
    self.intValue
  end

  def to_f
    self.doubleValue
  end
  
  def float?
    warn "#{caller[0]}: 'NSNumber#float?' is now deprecated and its use is discouraged, please use integer? instead."
    CFNumberIsFloatType(self)
  end
  
  def integer?
    !CFNumberIsFloatType(self)
  end
    
  def inspect
    "#<#{self.class} #{self.description}>"
  end
end

class NSDate
  def to_time
    Time.at(timeIntervalSince1970)
  end
  
  def inspect
    "#<#{self.class} #{self.description}>"
  end
end

class NSImage
  def focus
    lockFocus
    begin
      yield
    ensure
      unlockFocus
    end
  end
end

class NSRect
  class << self
    alias_method :orig_new, :new
    def new(*args)
      origin, size = case args.size
      when 0
        [[0, 0], [0, 0]]
      when 2
        [args[0], args[1]]
      when 4
        [args[0..1], args[2..3]]
      else
        raise ArgumentError, "wrong number of arguments (#{args.size} for either 0, 2 or 4)"
      end
      origin = NSPoint.new(*origin) unless origin.is_a?(NSPoint)
      size = NSSize.new(*size) unless size.is_a?(NSSize)
      orig_new(origin, size)
    end
  end
  
  def x; origin.x; end
  def y; origin.y; end
  def width; size.width; end
  def height; size.height; end
  def x=(v); origin.x = v; end
  def y=(v); origin.y = v; end
  def width=(v); size.width = v; end
  def height=(v); size.height = v; end
  alias_method :old_to_a, :to_a # To remove a warning.
  def to_a; [origin.to_a, size.to_a]; end
  def center; NSPoint.new(NSMidX(self), NSMidY(self)); end
  
  def contain?(arg)
    case arg
    when NSRect
      NSContainsRect(self, arg)
    when NSPoint
      NSPointInRect(arg, self)
    else
      raise ArgumentError, "argument should be NSRect or NSPoint"
    end
  end
  
  def empty?; NSIsEmptyRect(self); end
  def inflate(dx, dy); inset(-dx, -dy); end
  def inset(dx, dy); NSInsetRect(self, dx, dy); end
  def integral; NSIntegralRect(self); end
  def intersect?(rect); NSIntersectsRect(self, rect); end
  def intersection(rect); NSIntersectionRect(self, rect); end
  def offset(dx, dy); NSOffsetRect(self, dx, dy); end
  def union(rect); NSUnionRect(self, rect); end
  def inspect; "#<#{self.class} x=#{x}, y=#{y}, width=#{width}, height=#{height}>"; end
end

class NSPoint
  def in?(rect); NSPointInRect(self, rect); end
  alias_method :inRect?, :in?
  
  def +(v)
    if v.is_a?(NSSize)
      NSPoint.new(x + v.width, y + v.height)
    else
      raise ArgumentException, "parameter should be NSSize"
    end
  end
  
  def -(v)
    if v.is_a?(NSSize)
      NSPoint.new(x - v.width, y - v.height)
    else
      raise ArgumentException, "parameter should be NSSize"
    end
  end
  
  def inspect; "#<#{self.class} x=#{x}, y=#{y}>"; end
end

class NSSize
  def /(v); NSSize.new(width / v, height / v); end
  def *(v); NSSize.new(width * v, height * v); end
  def +(v); NSSize.new(width + v, height + v); end
  def -(v); NSSize.new(width - v, height - v); end
  def inspect; "#<#{self.class} width=#{width}, height=#{height}>"; end
end

class NSRange
  class << self
    alias_method :orig_new, :new
    def new(*args)
      location, length = case args.size
      when 0
        [0, 0]
      when 1
        if args.first.is_a?(Range)
          range = args.first
          [range.first, range.last - range.first + (range.exclude_end? ? 0 : 1)]
        else
          raise ArgumentError, "wrong type of argument #1 (expected Range, got #{args.first.class})"
        end
      when 2
        if args.first.is_a?(Range)
          range, count = args
          first = range.first
          first += count if first < 0
          last = range.last
          last += count if last < 0
          len = last - first + (range.exclude_end? ? 0 : 1)
          len = count - first if count < first + len
          len = 0 if len < 0
          [first, len]
        else
          [args[0], args[1]]
        end
      else
        raise ArgumentError, "wrong number of arguments (#{args.size} for either 0, 1 or 2)"
      end
      orig_new(location, length)
    end
  end
  
  def to_range
    Range.new(location, location + length, true)
  end
  
  def size; length; end
  def size=(v); length = v; end
  
  def contain?(arg)
    case arg
    when NSRange
      location <= arg.location and arg.location + arg.length <= location + length
    when Numeric
      NSLocationInRange(arg, self)
    else
      raise ArgumentError, "argument should be NSRange or Numeric"
    end
  end
  
  def empty?; length == 0 || not_found?; end
  def intersect?(range); !intersection(range).empty?; end
  def intersection(range); NSIntersectionRange(self, range); end
  def union(range); NSUnionRange(self, range); end
  def max; location + length; end
  def not_found?; location == NSNotFound; end
  def inspect; "#<#{self.class} location=#{location}, length=#{length}>"; end
end
