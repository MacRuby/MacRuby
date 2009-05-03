class TestMethodOverride < TestMethod
  def methodReturningVoid; 42; end
  def methodReturningSelf; self; end
  def methodReturningNil; nil; end
  def methodReturningCFTrue; true; end
  def methodReturningCFFalse; false; end
  def methodReturningYES; true; end
  def methodReturningNO; false; end
  def methodReturningChar; 42; end
  def methodReturningChar2; -42; end
  def methodReturningUnsignedChar; 42; end
  def methodReturningShort; 42; end
  def methodReturningShort2; -42; end
  def methodReturningUnsignedShort; 42; end
  def methodReturningInt; 42; end
  def methodReturningInt2; -42; end
  def methodReturningUnsignedInt; 42; end
  def methodReturningLong; 42; end
  def methodReturningLong2; -42; end
  def methodReturningUnsignedLong; 42; end
  def methodReturningFloat; 3.1415; end
  def methodReturningDouble; 3.1415; end
  def methodReturningSEL; :'foo:with:with:'; end
  def methodReturningSEL2; nil; end
  def methodReturningCharPtr; 'foo'; end
  def methodReturningCharPtr2; nil; end
  def methodReturningNSPoint; NSPoint.new(1, 2); end
  def methodReturningNSSize; NSSize.new(3, 4); end
  def methodReturningNSRect; NSRect.new(NSPoint.new(1, 2), NSSize.new(3, 4)); end
  def methodReturningNSRange; NSRange.new(0, 42); end
end
