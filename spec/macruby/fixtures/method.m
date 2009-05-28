#import <Foundation/Foundation.h>

@interface NSObject (TestInformalProtocolMethod)

- (int)informalProtocolMethod1:(int)arg;
- (BOOL)informalProtocolMethod2:(int)arg1 withValue:(int)arg2;

@end

@interface TestMethod : NSObject
{
    id _foo;
}
@end

@implementation TestMethod

- (BOOL)isFoo
{
    return YES;
}

- (BOOL)isFoo2
{
    return NO;
}

- (void)setFoo:(id)foo
{
    _foo = foo;
}

- (id)foo
{
    return _foo;
}

- (void)methodReturningVoid
{
}

+ (BOOL)testMethodReturningVoid:(TestMethod *)o
{
    [o methodReturningVoid];
    return YES;
}

- (id)methodReturningSelf
{
    return self;
}

+ (BOOL)testMethodReturningSelf:(TestMethod *)o
{
    return [o methodReturningSelf] == o;
}

- (id)methodReturningNil
{
    return nil;
}

+ (BOOL)testMethodReturningNil:(TestMethod *)o
{
    return [o methodReturningNil] == nil;
}

- (id)methodReturningCFTrue
{
    return (id)kCFBooleanTrue;
}

+ (BOOL)testMethodReturningCFTrue:(TestMethod *)o
{
    return [o methodReturningCFTrue] == (id)kCFBooleanTrue;
}

- (id)methodReturningCFFalse
{
    return (id)kCFBooleanFalse;
}

+ (BOOL)testMethodReturningCFFalse:(TestMethod *)o
{
    return [o methodReturningCFFalse] == (id)kCFBooleanFalse;
}

- (id)methodReturningCFNull
{
    return (id)kCFNull;
}

- (BOOL)methodReturningYES
{
    return YES;
}

+ (BOOL)testMethodReturningYES:(TestMethod *)o
{
    return [o methodReturningYES] == YES;
}

- (BOOL)methodReturningNO
{
    return NO;
}

+ (BOOL)testMethodReturningNO:(TestMethod *)o
{
    return [o methodReturningNO] == NO;
}

- (char)methodReturningChar
{
    return (char)42;
}

+ (BOOL)testMethodReturningChar:(TestMethod *)o
{
    return [o methodReturningChar] == (char)42;
}

- (char)methodReturningChar2
{
    return (char)-42;
}

+ (BOOL)testMethodReturningChar2:(TestMethod *)o
{
    return [o methodReturningChar2] == (char)-42;
}

- (unsigned char)methodReturningUnsignedChar
{
    return (unsigned char)42;
}

+ (BOOL)testMethodReturningUnsignedChar:(TestMethod *)o
{
    return [o methodReturningUnsignedChar] == (unsigned char)42;
}

- (short)methodReturningShort
{
    return (short)42;
}

+ (BOOL)testMethodReturningShort:(TestMethod *)o
{
    return [o methodReturningShort] == (short)42;
}

- (short)methodReturningShort2
{
    return (short)-42;
}

+ (BOOL)testMethodReturningShort2:(TestMethod *)o
{
    return [o methodReturningShort2] == (short)-42;
}

- (unsigned short)methodReturningUnsignedShort
{
    return (unsigned short)42;
}

+ (BOOL)testMethodReturningUnsignedShort:(TestMethod *)o
{
    return [o methodReturningUnsignedShort] == (unsigned short)42;
}

- (int)methodReturningInt
{
    return 42;
}

+ (BOOL)testMethodReturningInt:(TestMethod *)o
{
    return [o methodReturningInt] == (int)42;
}

- (int)methodReturningInt2
{
    return -42;
}

+ (BOOL)testMethodReturningInt2:(TestMethod *)o
{
    return [o methodReturningInt2] == (int)-42;
}

- (unsigned int)methodReturningUnsignedInt
{
    return 42;
}

+ (BOOL)testMethodReturningUnsignedInt:(TestMethod *)o
{
    return [o methodReturningUnsignedInt] == (int)42;
}

- (long)methodReturningLong
{
    return 42;
}

+ (BOOL)testMethodReturningLong:(TestMethod *)o
{
    return [o methodReturningLong] == (long)42;
}
 
- (long)methodReturningLong2
{
    return -42;
}

+ (BOOL)testMethodReturningLong2:(TestMethod *)o
{
    return [o methodReturningLong2] == (long)-42;
}

- (long)methodReturningLong3
{
#if __LP64__
    return 4611686018427387904;
#else
    return 1073741824;
#endif
}

- (long)methodReturningLong4
{
#if __LP64__
    return -4611686018427387905;
#else
    return -1073741825;
#endif
}

- (unsigned long)methodReturningUnsignedLong
{
    return 42;
}

+ (BOOL)testMethodReturningUnsignedLong:(TestMethod *)o
{
    return [o methodReturningUnsignedLong] == (unsigned long)42;
}

- (unsigned long)methodReturningUnsignedLong2
{
#if __LP64__
    return 4611686018427387904;
#else
    return 1073741824;
#endif
}

- (float)methodReturningFloat
{
    return 3.1415;
}

+ (BOOL)testMethodReturningFloat:(TestMethod *)o
{
    return [o methodReturningFloat] == (float)3.1415;
}

- (double)methodReturningDouble
{
    return 3.1415;
}

+ (BOOL)testMethodReturningDouble:(TestMethod *)o
{
    return [o methodReturningDouble] == (double)3.1415;
}

- (SEL)methodReturningSEL
{
    return @selector(foo:with:with:);
}

+ (BOOL)testMethodReturningSEL:(TestMethod *)o
{
    return [o methodReturningSEL] == @selector(foo:with:with:);
}

- (SEL)methodReturningSEL2
{
    return 0;
}

+ (BOOL)testMethodReturningSEL2:(TestMethod *)o
{
    return [o methodReturningSEL2] == NULL;
}

- (const char *)methodReturningCharPtr
{
    return "foo";
}

+ (BOOL)testMethodReturningCharPtr:(TestMethod *)o
{
    return strcmp([o methodReturningCharPtr], "foo") == 0;
}

- (const char *)methodReturningCharPtr2
{
    return NULL;
}

+ (BOOL)testMethodReturningCharPtr2:(TestMethod *)o
{
    return [o methodReturningCharPtr2] == NULL;
}

- (NSPoint)methodReturningNSPoint
{
    return NSMakePoint(1, 2);
}

+ (BOOL)testMethodReturningNSPoint:(TestMethod *)o
{
    return NSEqualPoints([o methodReturningNSPoint], NSMakePoint(1, 2));
}

- (NSSize)methodReturningNSSize
{
    return NSMakeSize(3, 4);
}

+ (BOOL)testMethodReturningNSSize:(TestMethod *)o
{
    return NSEqualSizes([o methodReturningNSSize], NSMakeSize(3, 4));
}

- (NSRect)methodReturningNSRect
{
    return NSMakeRect(1, 2, 3, 4);
}

+ (BOOL)testMethodReturningNSRect:(TestMethod *)o
{
    return NSEqualRects([o methodReturningNSRect], NSMakeRect(1, 2, 3, 4));
}

- (NSRange)methodReturningNSRange
{
    return NSMakeRange(0, 42);
}

+ (BOOL)testMethodReturningNSRange:(TestMethod *)o
{
    return NSEqualRanges([o methodReturningNSRange], NSMakeRange(0, 42));
}

- (BOOL)methodAcceptingSelf:(id)obj
{
    return obj == self;
}

+ (BOOL)testMethodAcceptingSelf:(TestMethod *)o
{
    return [o methodAcceptingSelf:o];
}

- (BOOL)methodAcceptingSelfClass:(id)obj
{
    return obj == [self class];
}

+ (BOOL)testMethodAcceptingSelfClass:(TestMethod *)o
{
    return [o methodAcceptingSelfClass:[o class]];
}

- (BOOL)methodAcceptingNil:(id)obj
{
    return obj == nil;
}

+ (BOOL)testMethodAcceptingNil:(TestMethod *)o
{
    return [o methodAcceptingNil:nil];
}

- (BOOL)methodAcceptingTrue:(id)obj
{
    return obj == (id)kCFBooleanTrue;
}

+ (BOOL)testMethodAcceptingTrue:(TestMethod *)o
{
    return [o methodAcceptingTrue:(id)kCFBooleanTrue];
}

- (BOOL)methodAcceptingFalse:(id)obj
{
    return obj == (id)kCFBooleanFalse;
}

+ (BOOL)testMethodAcceptingFalse:(TestMethod *)o
{
    return [o methodAcceptingFalse:(id)kCFBooleanFalse];
}

- (BOOL)methodAcceptingFixnum:(id)obj
{
    return [obj intValue] == 42;
}

+ (BOOL)testMethodAcceptingFixnum:(TestMethod *)o
{
    return [o methodAcceptingFixnum:[NSNumber numberWithInt:42]];
}

- (BOOL)methodAcceptingChar:(char)c
{
    return c == 42;
}

+ (BOOL)testMethodAcceptingChar:(TestMethod *)o
{
    return [o methodAcceptingChar:42];
}

- (BOOL)methodAcceptingUnsignedChar:(unsigned char)c
{
    return c == 42;
}

+ (BOOL)testMethodAcceptingUnsignedChar:(TestMethod *)o
{
    return [o methodAcceptingUnsignedChar:42];
}

- (BOOL)methodAcceptingShort:(short)c
{
    return c == 42;
}

+ (BOOL)testMethodAcceptingShort:(TestMethod *)o
{
    return [o methodAcceptingShort:42];
}

- (BOOL)methodAcceptingUnsignedShort:(unsigned short)c
{
    return c == 42;
}

+ (BOOL)testMethodAcceptingUnsignedShort:(TestMethod *)o
{
    return [o methodAcceptingUnsignedShort:42];
}

- (BOOL)methodAcceptingInt:(int)c
{
    return c == 42;
}

+ (BOOL)testMethodAcceptingInt:(TestMethod *)o
{
    return [o methodAcceptingInt:42];
}

- (BOOL)methodAcceptingUnsignedInt:(unsigned int)c
{
    return c == 42;
}

+ (BOOL)testMethodAcceptingUnsignedInt:(TestMethod *)o
{
    return [o methodAcceptingUnsignedInt:42];
}

- (BOOL)methodAcceptingLong:(long)c
{
    return c == 42;
}

+ (BOOL)testMethodAcceptingLong:(TestMethod *)o
{
    return [o methodAcceptingLong:42];
}

- (BOOL)methodAcceptingUnsignedLong:(unsigned long)c
{
    return c == 42;
}

+ (BOOL)testMethodAcceptingUnsignedLong:(TestMethod *)o
{
    return [o methodAcceptingUnsignedLong:42];
}

- (BOOL)methodAcceptingTrueBOOL:(BOOL)b
{
    return b == YES;
}

+ (BOOL)testMethodAcceptingTrueBOOL:(TestMethod *)o
{
    return [o methodAcceptingTrueBOOL:YES];
}

- (BOOL)methodAcceptingFalseBOOL:(BOOL)b
{
    return b == NO;
}

+ (BOOL)testMethodAcceptingFalseBOOL:(TestMethod *)o
{
    return [o methodAcceptingFalseBOOL:NO];
}

- (BOOL)methodAcceptingSEL:(SEL)sel
{
    return sel == @selector(foo:with:with:);
}

+ (BOOL)testMethodAcceptingSEL:(TestMethod *)o
{
    return [o methodAcceptingSEL:@selector(foo:with:with:)];
}

- (BOOL)methodAcceptingSEL2:(SEL)sel
{
    return sel == 0;
}

+ (BOOL)testMethodAcceptingSEL2:(TestMethod *)o
{
    return [o methodAcceptingSEL2:0];
}

- (BOOL)methodAcceptingCharPtr:(const char *)s
{
    return strcmp(s, "foo") == 0;
}

+ (BOOL)testMethodAcceptingCharPtr:(TestMethod *)o
{
    return [o methodAcceptingCharPtr:"foo"];
}

- (BOOL)methodAcceptingCharPtr2:(const char *)s
{
    return s == NULL;
}

+ (BOOL)testMethodAcceptingCharPtr2:(TestMethod *)o
{
    return [o methodAcceptingCharPtr2:NULL];
}

- (BOOL)methodAcceptingFloat:(float)f
{
    return f > 3.1414 && f < 3.1416;
}

+ (BOOL)testMethodAcceptingFloat:(TestMethod *)o
{
    return [o methodAcceptingFloat:3.1415];
}

- (BOOL)methodAcceptingDouble:(double)d
{
    return d > 3.1414 && d < 3.1416;
}

+ (BOOL)testMethodAcceptingDouble:(TestMethod *)o
{
    return [o methodAcceptingDouble:3.1415];
}

- (BOOL)methodAcceptingNSPoint:(NSPoint)p
{
    return p.x == 1 && p.y == 2;
}

+ (BOOL)testMethodAcceptingNSPoint:(TestMethod *)o
{
    return [o methodAcceptingNSPoint:NSMakePoint(1, 2)];
}

- (BOOL)methodAcceptingNSSize:(NSSize)s
{
    return s.width == 3 && s.height == 4;
}

+ (BOOL)testMethodAcceptingNSSize:(TestMethod *)o
{
    return [o methodAcceptingNSSize:NSMakeSize(3, 4)];
}

- (BOOL)methodAcceptingNSRect:(NSRect)r
{
    return r.origin.x == 1 && r.origin.y == 2 && r.size.width == 3
	&& r.size.height == 4;
}

+ (BOOL)testMethodAcceptingNSRect:(TestMethod *)o
{
    return [o methodAcceptingNSRect:NSMakeRect(1, 2, 3, 4)];
}

- (BOOL)methodAcceptingNSRange:(NSRange)r
{
    return r.location == 0 && r.length == 42;
}

+ (BOOL)testMethodAcceptingNSRange:(TestMethod *)o
{
    return [o methodAcceptingNSRange:NSMakeRange(0, 42)];
}

- (BOOL)methodAcceptingInt:(int)a1 float:(float)a2 double:(double)a3
	short:(short)a4 NSPoint:(NSPoint)a5 NSRect:(NSRect)a6 char:(char)a7
{
    return a1 == 42 && a2 == 42.0 && a3 == 42.0 && a4 == 42
	&& a5.x == 42.0 && a5.y == 42.0
	&& a6.origin.x == 42.0 && a6.origin.y == 42.0
	&& a6.size.width == 42.0 && a6.size.height == 42.0
	&& a7 == 42;
}

+ (BOOL)testMethodAcceptingComplexTypes:(TestMethod *)o
{
    return [o methodAcceptingInt:42 float:42 double:42.0 short:42
	NSPoint:NSMakePoint(42, 42) NSRect:NSMakeRect(42, 42, 42, 42)
	char:42];
}

- (BOOL)methodAcceptingIntPtr:(int *)ptr
{
    BOOL ok = ptr[0] == 42;
    ptr[0] = 43;
    return ok;
}

- (BOOL)methodAcceptingIntPtr2:(int *)ptr
{
    return ptr == NULL;
}

- (BOOL)methodAcceptingObjectPtr:(id *)ptr
{
    BOOL ok = ptr[0] == self;
    ptr[0] = (id)[NSObject class];
    return ok;
}

- (BOOL)methodAcceptingObjectPtr2:(id *)ptr
{
    return ptr == NULL;
}

- (BOOL)methodAcceptingNSRectPtr:(NSRect *)ptr
{
    BOOL ok = ptr->origin.x == 1 && ptr->origin.y == 2
	&& ptr->size.width == 3 && ptr->size.height == 4;
    *ptr = NSMakeRect(42, 43, 44, 45);
    return ok;
}

- (BOOL)methodAcceptingNSRectPtr2:(NSRect *)ptr
{
    return ptr == NULL;
}

+ (BOOL)testInformalProtocolMethod1:(id)o
{
    return [o informalProtocolMethod1:41] == 42;
}

+ (BOOL)testInformalProtocolMethod2:(id)o
{
    return [o informalProtocolMethod2:40 withValue:2];
}

@end

void
Init_method(void)
{
    // Do nothing.
}
