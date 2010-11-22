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

@protocol AnotherTestProtocol

+ (int)anotherClassMethod;
- (int)anotherInstanceMethod;

@optional

+ (int)anotherOptionalClassMethod;
- (int)anotherOptionalInstanceMethod;

@end

@protocol TestProtocol <AnotherTestProtocol>

+ (int)aClassMethod;
+ (int)aClassMethodWithArg:(int)arg;
+ (int)aClassMethodWithArg:(int)arg1 anotherArg:(int)arg2;

- (int)anInstanceMethod;
- (int)anInstanceMethodWithArg:(int)arg;
- (int)anInstanceMethodWithArg:(int)arg1 anotherArg:(int)arg2;

@optional

+ (int)anOptionalClassMethod;
- (int)anOptionalInstanceMethod;

@end

@interface TestProtocolConformance : NSObject
@end

@implementation TestProtocolConformance

+ (BOOL)checkIfObjectConformsToTestProtocol:(id)object
{
  return [object conformsToProtocol:@protocol(TestProtocol)];
}

@end

void Init_object(void) {}
