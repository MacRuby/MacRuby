/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2010, Apple Inc. All rights reserved
 */

#if __OBJC__

#import <Foundation/Foundation.h>

@interface MacRuby : NSObject

/* Get a singleton reference to the MacRuby runtime, initializing it before if 
 * needed. The same instance is re-used after.
 */
+ (MacRuby *)sharedRuntime;

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

#endif /* __OBJC__ */
