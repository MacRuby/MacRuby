#import <Foundation/Foundation.h>

@interface MacRuby : NSObject

/* Get a singleton reference to the MacRuby runtime, initializing it before if 
 * needed. The same instance is re-used after.
 */
+ (MacRuby *)sharedRuntime;

/* Connect and attach a MacRuby runtime to the given process and return a 
 * reference to it. This is done using mach_inject.
 */
+ (MacRuby *)runtimeAttachedToProcessIdentifier:(pid_t)pid;

/* Evaluate a Ruby expression in the given file and return a reference to the 
 * result. 
 */
- (id)evaluateFileAtPath:(NSString *)path;

/* Evaluate a Ruby expression in the given URL and return a reference to the 
 * result. Currently only file:// URLs are supported.
 */
- (id)evaluateFileAtURL:(NSURL *)URL;

/* Evaluate a Ruby expression in the given string and return a reference to the
 * result.
 */
- (id)evaluateString:(NSString *)expression;

/* Load the BridgeSupport file at the given path. 
 */
- (void)loadBridgeSupportFileAtPath:(NSString *)path;

/* Load the BridgeSupport file at the given URL. 
 */
- (void)loadBridgeSupportFileAtURL:(NSURL *)URL;

@end

@interface NSObject (MacRubyAdditions)

/* Perform the given selector and return a reference to the result. */
- (id)performRubySelector:(SEL)sel;

/* Perform the given selector, passing the given arguments and return a
 * reference to the result. The argv argument should be a C array whose size
 * is the value of the argc argument.
 */
- (id)performRubySelector:(SEL)sel withArguments:(id *)argv count:(int)argc;

/* Perform the given selector, passing the given arguments and return a 
 * reference to the result. The arguments should be a NULL-terminated list.
 */
- (id)performRubySelector:(SEL)sel withArguments:(id)firstArgument, ...;

@end
