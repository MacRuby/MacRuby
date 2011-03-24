#import <Foundation/Foundation.h>

@interface TestException : NSObject
@end

@implementation TestException

+ (void)raiseObjCException
{
    [NSException raise:@"SinkingShipException" format:@"the ship is sinking!"];
}

+ (BOOL)catchRubyException:(id)obj
{
    @try {
	[obj performSelector:@selector(raiseRubyException)];
    }
    @catch (id e) {
	return YES;
    }
    return NO;
}

@end

void Init_exception(void) {}
