class Wrapper
  attr_accessor :whatever

  def initialize(value = nil)
    super()
    @wrapped = value
    @whatever= 'like, whatever'
  end

  def wrappedValue
    @wrapped
  end
end

class FancyWrapper < NSValue
  attr_accessor :whatever

  def initialize(value)
    super()
    @wrapped = value
    @whatever= 'like, whatever'
  end

  def wrappedValue
    @wrapped
  end
end

class Wrapper
  attr_accessor :whatever
  
  def kvoDelegate=(delegate)
    @kvoDelegate = delegate
  end
  
  def willChangeValueForKey(key)
    @kvoDelegate.wrapperWillChangeValueForKey(key) if @kvoDelegate
  end
  
  def didChangeValueForKey(key)
    @kvoDelegate.wrapperDidChangeValueForKey(key) if @kvoDelegate
  end
end