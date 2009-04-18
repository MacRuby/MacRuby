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

@end

void
Init_method(void)
{
    // Do nothing.
}
