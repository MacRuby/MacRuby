#import <Foundation/Foundation.h>

@interface TestMethod : NSObject
@end

@implementation TestMethod

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

@end

void
Init_method(void)
{
    // Do nothing.
}
