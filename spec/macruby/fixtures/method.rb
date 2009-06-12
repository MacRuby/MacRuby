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

  def methodAcceptingSelf(a); super; end
  def methodAcceptingSelfClass(a); super; end
  def methodAcceptingNil(a); super; end
  def methodAcceptingTrue(a); super; end
  def methodAcceptingFalse(a); super; end
  def methodAcceptingFixnum(a); super; end
  def methodAcceptingChar(a); super; end
  def methodAcceptingUnsignedChar(a); super; end
  def methodAcceptingShort(a); super; end
  def methodAcceptingUnsignedShort(a); super; end
  def methodAcceptingInt(a); super; end
  def methodAcceptingUnsignedInt(a); super; end
  def methodAcceptingLong(a); super; end
  def methodAcceptingUnsignedLong(a); super; end
  def methodAcceptingTrueBOOL(a); super; end
  def methodAcceptingFalseBOOL(a); super; end
  def methodAcceptingSEL(a); super; end
  def methodAcceptingSEL2(a); super; end
  def methodAcceptingCharPtr(a); super; end
  def methodAcceptingCharPtr2(a); super; end
  def methodAcceptingFloat(a); super; end
  def methodAcceptingDouble(a); super; end
  def methodAcceptingNSPoint(a); super; end
  def methodAcceptingNSSize(a); super; end
  def methodAcceptingNSRect(a); super; end
  def methodAcceptingNSRange(a); super; end
  def methodAcceptingObjPtr(a); super; end
  def methodAcceptingObjPtr2(a); super; end
  def methodAcceptingInt(a, float:a2, double:a3, short:a4, NSPoint:a5,
                         NSRect:a6, char:a7); super; end
end

class TestInformalProtocolMethod
  def informalProtocolMethod1(x)
    x + 1
  end
  def informalProtocolMethod2(x, withValue:x2)
    x + x2 == 42
  end
end
