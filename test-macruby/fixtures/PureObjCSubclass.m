#import <Foundation/Foundation.h>

@interface PureObjCSubclass : NSObject
@end

@implementation PureObjCSubclass
- (id)require:(NSString *)name
{
    return [name uppercaseString];
}

- (id)test_getObject:(id)dictionary forKey:(id)key
{
    return [dictionary objectForKey:key];
}
@end