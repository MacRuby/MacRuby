/*
 * MacRuby Debugger Connector.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2010-2011, Apple Inc. All rights reserved.
 */

#import "MacRubyDebuggerConnector.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

@implementation MacRubyDebuggerConnector

- (NSData *)_recv
{
    assert(_socket != nil);
    return [_socket availableData];
}

- (const char *)_recvCString
{
    return (const char *)[[self _recv] bytes];
}

- (NSString *)_recvString
{
    return [[NSString alloc] initWithData:[self _recv]
	encoding:NSASCIIStringEncoding];
}

- (void)_send:(NSData *)data
{
    assert(_socket != nil);
    [_socket writeData:data]; 
}

- (void)_sendCString:(const char *)str
{
    [self _send:[NSData dataWithBytes:str length:strlen(str)]];
}

-(void)_readLocation
{
    _location = [self _recvString];
    if ([_location length] == 0) {
	_location = nil;
    }
}

- (id)initWithInterpreterPath:(NSString *)ipath arguments:(NSArray *)arguments
{
    self = [super init];
    if (self != nil) {
	// Generate the socket path.
	NSString *tmpdir = NSTemporaryDirectory();
	assert(tmpdir != nil);
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/macrubyd-XXXXXX",
		[tmpdir fileSystemRepresentation]);
	assert(mktemp(path) != NULL);
	_socketPath = [[NSFileManager defaultManager]
	    stringWithFileSystemRepresentation:path length:strlen(path)];

	// Setup arguments.
	_arguments = [NSMutableArray new];
	[_arguments addObject:@"--debug-mode"];
	[_arguments addObject:_socketPath];
	[_arguments addObjectsFromArray:arguments];

	_interpreterPath = ipath;
	_task = nil;
	_socket = nil;
	_location = nil;
    }
    return self;
}

- (void)finalize
{
    [super finalize];
    [self stopExecution];
}

- (void)startExecution
{
    if (_task == nil || ![_task isRunning]) {
	// Create task.
	_task = [NSTask new];
	[_task setLaunchPath:_interpreterPath];
	[_task setArguments:_arguments];

	// Launch the remote process.
	[[NSFileManager defaultManager] removeItemAtPath:_socketPath error:nil];
	[_task launch];

	// Wait until the socket file is available.
	while (true) {
	    if ([[NSFileManager defaultManager] fileExistsAtPath:_socketPath]) {
		break;
	    }
	    if (![_task isRunning]) {
		// Something wrong happened at the very beginning, most likely
		// a command-line argument error.
		_task = nil;
		return;
	    }
	    assert([_task isRunning]); // XXX raise exception
	    usleep(500000);
	}

	// Create the socket.
	const int fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1) {
	    // XXX should raise exception.
	    perror("socket()");
	    exit(1);
	}

	// Prepare the name.
	struct sockaddr_un name;
	name.sun_family = PF_LOCAL;
	strncpy(name.sun_path, [_socketPath fileSystemRepresentation],
		sizeof(name.sun_path));

	// Connect.
	if (connect(fd, (struct sockaddr *)&name, SUN_LEN(&name)) == -1) {
	    // XXX should raise exception.
	    perror("connect()");
	    exit(1);
	}

	// Good!
	_socket = [[NSFileHandle alloc] initWithFileDescriptor:fd];
	[self _readLocation];
    }
}

- (void)continueExecution
{
    [self _sendCString:"continue"];
    [self _readLocation];
}

- (void)stepExecution
{
    [self _sendCString:"next"];
    [self _readLocation];
}

- (void)stopExecution
{
    if (_task != nil) {
	[_task terminate];
	_task = nil;
    }
    _location = nil;
    [[NSFileManager defaultManager] removeItemAtPath:_socketPath error:nil];
}

- (NSString *)location
{
    return _location;
}

- (NSArray *)localVariables
{
    return nil;
}

- (NSArray *)backtrace
{
    [self _sendCString:"backtrace"];
    return [[self _recvString] componentsSeparatedByString:@"\n"];
}

- (void)setFrame:(unsigned int)frame
{
    char buf[512];
    snprintf(buf, sizeof buf, "frame %d", frame);
    [self _sendCString:buf];
}

- (breakpoint_t)addBreakPointAtPath:(NSString *)path line:(unsigned int)line
	condition:(NSString *)condition
{
    char buf[512];
    snprintf(buf, sizeof buf, "break %s:%d", [path fileSystemRepresentation],
	    line);
    [self _sendCString:buf];
    return [[self _recvString] integerValue];
}

- (void)enableBreakPoint:(breakpoint_t)bp
{
    char buf[512];
    snprintf(buf, sizeof buf, "enable %d", bp);
    [self _sendCString:buf];
}

- (void)disableBreakPoint:(breakpoint_t)bp
{
    char buf[512];
    snprintf(buf, sizeof buf, "disable %d", bp);
    [self _sendCString:buf];
}

- (void)deleteBreakPoint:(breakpoint_t)bp
{
    char buf[512];
    snprintf(buf, sizeof buf, "delete %d", bp);
    [self _sendCString:buf];
}

- (void)setCondition:(NSString *)condition forBreakPoint:(breakpoint_t)bp
{
    char buf[512];
    snprintf(buf, sizeof buf, "condition %d %s", bp, [condition UTF8String]);
    [self _sendCString:buf];
}

- (NSDictionary *)_parseBreakPointDescription:(NSString *)desc
{
    NSArray *ary = [desc componentsSeparatedByString:@","];
    if ([ary count] == 0) {
	return nil;
    }
    NSMutableDictionary *dict = [NSMutableDictionary new];
    unsigned i, count;
    for (i = 0, count = [ary count]; i < count; i++) {
	NSArray *fields = [[ary objectAtIndex:i]
	    componentsSeparatedByString:@"="];
	if ([fields count] == 2) {
	    NSString *key = [fields objectAtIndex:0];
	    NSString *val = [fields objectAtIndex:1];
	    [dict setObject:val forKey:key];
	}
    }
    if ([dict count] == 0) {
	return nil;
    }
    return dict; 
}


- (NSArray *)allBreakPoints
{
    [self _sendCString:"info breakpoints"];
    NSArray *ary = [[self _recvString] componentsSeparatedByString:@"\n"];
    NSMutableArray *ary2 = [NSMutableArray new];
    unsigned i, count;
    for (i = 0, count = [ary count]; i < count; i++) {
	NSString *desc = [ary objectAtIndex:i];
	NSDictionary *dict = [self _parseBreakPointDescription:desc];
	if (dict != nil) {
	    [ary2 addObject:dict];
	}
    }
    return ary2;
}

- (NSString *)evaluateExpression:(NSString *)expression
{
    char buf[512];
    snprintf(buf, sizeof buf, "eval %s", [expression UTF8String]);
    [self _sendCString:buf];
    return [self _recvString];
}

@end
