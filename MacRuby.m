/*
 * MacRuby Objective-C API.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2009-2011, Apple Inc. All rights reserved.
 */

#import <Foundation/Foundation.h>

#include "macruby_internal.h"
#include "ruby/node.h"
#include "ruby/objc.h"
#include "vm.h"
#include "objc.h"

@implementation MacRuby

extern bool ruby_initialized;

+ (MacRuby *)sharedRuntime
{
    static MacRuby *runtime = nil;
    if (runtime == nil) {
	runtime = [[MacRuby alloc] init];
	if (!ruby_initialized) {
	    int argc = 0;
	    char **argv = NULL;
	    ruby_sysinit(&argc, &argv);
	    ruby_init();
	    ruby_init_loadpath();
	    rb_vm_init_compiler();
	    rb_vm_init_jit();
	    rb_objc_fix_relocatable_load_path();
	    rb_objc_load_loaded_frameworks_bridgesupport();
	}
    }
    return runtime;
}

- (id)evaluateString:(NSString *)expression
{
    return RB2OC(rb_eval_string([(NSString *)expression UTF8String]));
}

- (id)evaluateFileAtPath:(NSString *)path
{
    return RB2OC(rb_f_require(Qnil, (VALUE)path));
}

- (id)evaluateFileAtURL:(NSURL *)URL
{
    if (![URL isFileURL]) {
	[NSException raise:NSInvalidArgumentException format:
	    @"given URL is not a file URL"];
    }
    return [self evaluateFileAtPath:[URL relativePath]];
}

- (void)loadBridgeSupportFileAtPath:(NSString *)path
{
#if MACRUBY_STATIC
    printf("loadBridgeSupportFileAtPath: not supported in MacRuby static\n");
    abort();
#else
    rb_vm_load_bridge_support([path fileSystemRepresentation], NULL, 0);
#endif
}

- (void)loadBridgeSupportFileAtURL:(NSURL *)URL
{
    if (![URL isFileURL]) {
	[NSException raise:NSInvalidArgumentException format:
	    @"given URL is not a file URL"];
    }
    [self loadBridgeSupportFileAtPath:[URL relativePath]];
}

@end

@implementation NSObject (MacRubyAdditions)

- (id)performRubySelector:(SEL)sel
{
    return [self performRubySelector:sel withArguments:NULL];
}

- (id)performRubySelector:(SEL)sel withArguments:(id *)argv count:(int)argc
{
    VALUE *rargv = NULL;
    if (argc > 0) {
	rargv = (VALUE *)xmalloc(sizeof(VALUE) * argc);
	for (int i = 0; i < argc; i++) {
	    rargv[i] = OC2RB(argv[i]);
	}
    }

    return RB2OC(rb_vm_call(OC2RB(self), sel, argc, rargv));
}

- (id)performRubySelector:(SEL)sel withArguments:firstArg, ...
{
    va_list args;
    int argc;
    id *argv;

    if (firstArg != nil) {
	argc = 1;
	va_start(args, firstArg);
	while (va_arg(args, id) != NULL) {
	    argc++;
	}
	va_end(args);
	argv = xmalloc(sizeof(id) * argc);
	va_start(args, firstArg);
	argv[0] = firstArg;
	for (int i = 1; i < argc; i++) {
	    argv[i] = va_arg(args, id);
	}
	va_end(args);
    }
    else {
	argc = 0;
	argv = NULL;
    }

    return [self performRubySelector:sel withArguments:argv count:argc];
}

@end
