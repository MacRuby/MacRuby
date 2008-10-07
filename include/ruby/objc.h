#import <Foundation/Foundation.h>

@interface MacRuby : NSObject

+ (MacRuby *)sharedRuntime;
+ (MacRuby *)runtimeAttachedToProcessIdentifier:(pid_t)pid;

- (id)evaluateFileAtPath:(NSString *)path;
- (id)evaluateFileAtURL:(NSURL *)URL;
- (id)evaluateString:(NSString *)expression;

@end
