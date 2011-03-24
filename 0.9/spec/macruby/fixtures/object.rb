class TestObjectPureMacRuby
  def initialize
    @initialized = true
  end
  def initialized?; @initialized; end
end

class TestObjectThatDoesNotCompletelyConformToProtocol
  def anInstanceMethod
  end

  def anInstanceMethodWithArg(arg)
  end

  def anInstanceMethodWithArg(arg1, anotherArg: arg2)
  end

  def self.aClassMethod
  end

  def self.aClassMethodWithArg(arg)
  end

  def self.aClassMethodWithArg(arg1, anotherArg: arg2)
  end
end

class TestObjectThatConformsToProtocol < TestObjectThatDoesNotCompletelyConformToProtocol
  def anotherInstanceMethod
  end

  def self.anotherClassMethod
  end
end
