#import <Foundation/Foundation.h>

@interface TestMethod : NSObject
@end

@implementation TestMethod

- (id)methodReturningSelf
{
    return self;
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

@end

void
Init_method(void)
{
    // Do nothing.
}
