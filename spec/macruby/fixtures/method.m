#import <Foundation/Foundation.h>

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

- (id)methodReturningSelf
{
    return self;
}

- (id)methodReturningNil
{
    return nil;
}

- (id)methodReturningCFTrue
{
    return (id)kCFBooleanTrue;
}

- (id)methodReturningCFFalse
{
    return (id)kCFBooleanFalse;
}

- (id)methodReturningCFNull
{
    return (id)kCFNull;
}

- (BOOL)methodReturningYES
{
    return YES;
}

- (BOOL)methodReturningNO
{
    return NO;
}

- (char)methodReturningChar
{
    return (char)42;
}

- (char)methodReturningChar2
{
    return (char)-42;
}

- (unsigned char)methodReturningUnsignedChar
{
    return (unsigned char)42;
}

- (short)methodReturningShort
{
    return (short)42;
}

- (short)methodReturningShort2
{
    return (short)-42;
}

- (unsigned short)methodReturningUnsignedShort
{
    return (unsigned short)42;
}

- (int)methodReturningInt
{
    return 42;
}

- (int)methodReturningInt2
{
    return -42;
}

- (unsigned int)methodReturningUnsignedInt
{
    return 42;
}

- (long)methodReturningLong
{
    return 42;
}
 
- (long)methodReturningLong2
{
    return -42;
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

- (double)methodReturningDouble
{
    return 3.1415;
}

- (SEL)methodReturningSEL
{
    return @selector(foo:with:with:);
}

- (SEL)methodReturningSEL2
{
    return 0;
}

- (const char *)methodReturningCharPtr
{
    return "foo";
}

- (const char *)methodReturningCharPtr2
{
    return NULL;
}

- (NSPoint)methodReturningNSPoint
{
    return NSMakePoint(1, 2);
}

- (NSSize)methodReturningNSSize
{
    return NSMakeSize(3, 4);
}

- (NSRect)methodReturningNSRect
{
    return NSMakeRect(1, 2, 3, 4);
}

- (NSRange)methodReturningNSRange
{
    return NSMakeRange(0, 42);
}

- (BOOL)methodAcceptingSelf:(id)obj
{
    return obj == self;
}

- (BOOL)methodAcceptingSelfClass:(id)obj
{
    return obj == [self class];
}

- (BOOL)methodAcceptingNil:(id)obj
{
    return obj == nil;
}

- (BOOL)methodAcceptingTrue:(id)obj
{
    return obj == (id)kCFBooleanTrue;
}

- (BOOL)methodAcceptingFalse:(id)obj
{
    return obj == (id)kCFBooleanFalse;
}

- (BOOL)methodAcceptingFixnum:(id)obj
{
    return [obj intValue] == 42;
}

- (BOOL)methodAcceptingChar:(char)c
{
    return c == 42;
}

- (BOOL)methodAcceptingUnsignedChar:(unsigned char)c
{
    return c == 42;
}

- (BOOL)methodAcceptingShort:(short)c
{
    return c == 42;
}

- (BOOL)methodAcceptingUnsignedShort:(unsigned short)c
{
    return c == 42;
}

- (BOOL)methodAcceptingInt:(int)c
{
    return c == 42;
}

- (BOOL)methodAcceptingUnsignedInt:(unsigned int)c
{
    return c == 42;
}

- (BOOL)methodAcceptingLong:(long)c
{
    return c == 42;
}

- (BOOL)methodAcceptingUnsignedLong:(unsigned long)c
{
    return c == 42;
}

- (BOOL)methodAcceptingTrueBOOL:(BOOL)b
{
    return b == YES;
}

- (BOOL)methodAcceptingFalseBOOL:(BOOL)b
{
    return b == NO;
}

- (BOOL)methodAcceptingSEL:(SEL)sel
{
    return sel == @selector(foo:with:with:);
}

- (BOOL)methodAcceptingSEL2:(SEL)sel
{
    return sel == 0;
}

- (BOOL)methodAcceptingCharPtr:(const char *)s
{
    return strcmp(s, "foo") == 0;
}

- (BOOL)methodAcceptingCharPtr2:(const char *)s
{
    return s == NULL;
}

- (BOOL)methodAcceptingFloat:(float)f
{
    return f > 3.1414 && f < 3.1416;
}

- (BOOL)methodAcceptingDouble:(double)d
{
    return d > 3.1414 && d < 3.1416;
}

- (BOOL)methodAcceptingNSPoint:(NSPoint)p
{
    return p.x == 1 && p.y == 2;
}

- (BOOL)methodAcceptingNSSize:(NSSize)s
{
    return s.width == 3 && s.height == 4;
}

- (BOOL)methodAcceptingNSRect:(NSRect)r
{
    return r.origin.x == 1 && r.origin.y == 2 && r.size.width == 3
	&& r.size.height == 4;
}

- (BOOL)methodAcceptingNSRange:(NSRange)r
{
    return r.location == 0 && r.length == 42;
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

@end

void
Init_method(void)
{
    // Do nothing.
}
