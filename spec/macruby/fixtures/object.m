#import <Foundation/Foundation.h>

@interface TestObject : NSObject
@end

@implementation TestObject

+ (id)testNewObject:(Class)k
{
    return [k new];
}

+ (id)testAllocInitObject:(Class)k
{
    return [[k alloc] init];
}

+ (id)testAllocWithZoneInitObject:(Class)k
{
    return [[k allocWithZone:NULL] init];
}

@end

@interface TestClassInitialization : NSObject
@end

@implementation TestClassInitialization

static int res = 0;

+ (void)initialize
{
    if (self == [TestClassInitialization class]) {
	res += 42;
    }
}

+ (int)result
{
    return res;
}

@end

@interface TestCustomMethodResolver : NSObject
@end

@implementation TestCustomMethodResolver

+ (BOOL)resolveInstanceMethod:(SEL)name
{
    return NO;
}

+ (BOOL)resolveClassMethod:(SEL)name
{
    return NO;
}

@end

void Init_object(void) {}
