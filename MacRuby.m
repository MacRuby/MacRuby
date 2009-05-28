#import <Foundation/Foundation.h>
#include "ruby/ruby.h"
#include "ruby/node.h"
#include "ruby/objc.h"
#include "vm.h"

@implementation MacRuby

extern int ruby_initialized;

+ (MacRuby *)sharedRuntime
{
    static MacRuby *runtime = nil;
    if (runtime == nil) {
	runtime = [[MacRuby alloc] init];
	if (ruby_initialized == 0) {
	    int argc = 0;
	    char **argv = NULL;
	    ruby_sysinit(&argc, &argv);
	    ruby_init();
	}
    }
    return runtime;
}

+ (MacRuby *)runtimeAttachedToProcessIdentifier:(pid_t)pid
{
    [NSException raise:NSGenericException format:@"not implemented yet"];
    return nil;
}

- (id)evaluateString:(NSString *)expression
{
    return RB2OC(rb_eval_string([(NSString *)expression UTF8String]));
}

- (id)evaluateFileAtPath:(NSString *)path
{
    return [self evaluateString:[NSString stringWithContentsOfFile:path
	usedEncoding:nil error:nil]];
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
    rb_vm_load_bridge_support([path fileSystemRepresentation], NULL, 0);
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
	rargv = (VALUE *)alloca(sizeof(VALUE) * argc);
	int i;
	for (i = 0; i < argc; i++) {
	    rargv[i] = OC2RB(argv[i]);
	}
    }

    return RB2OC(rb_vm_call(OC2RB(self), sel, argc, rargv, false));
}

- (id)performRubySelector:(SEL)sel withArguments:firstArg, ...
{
    va_list args;
    int argc;
    id *argv;

    if (firstArg != nil) {
	int i;

	argc = 1;
	va_start(args, firstArg);
	while (va_arg(args, id) != NULL) {
	    argc++;
	}
	va_end(args);
	argv = alloca(sizeof(id) * argc);
	va_start(args, firstArg);
	argv[0] = firstArg;
	for (i = 1; i < argc; i++) {
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
