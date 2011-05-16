#import <Foundation/Foundation.h>

@interface TestException : NSObject
@end

@implementation TestException

+ (void)raiseObjCException
{
    [NSException raise:@"SinkingShipException" format:@"the ship is sinking!"];
}

+ (id)catchRubyException:(id)obj
{
    @try {
	[obj performSelector:@selector(raiseRubyException)];
    }
    @catch (id e) {
	return e;
    }
    return nil;
}

+ (NSString *)catchRubyExceptionAndReturnName:(id)obj
{
  return [[self catchRubyException:obj] name];
}

+ (NSString *)catchRubyExceptionAndReturnReason:(id)obj
{
  return [[self catchRubyException:obj] reason];
}

@end

void Init_exception(void) {}
