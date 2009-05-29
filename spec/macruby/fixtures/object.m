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

void Init_object(void) {}
