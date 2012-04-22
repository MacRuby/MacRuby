/*
 * MacRuby Debugger Connector.
 *
 * This is an interface to the MacRuby runtime when running in debug mode.
 * It is used by MacRuby debugger clients, such as macrubyd or Xcode.
 * This file when compiled separately must be compiled with garbage collection
 * enabled.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2010-2011, Apple Inc. All rights reserved.
 */

#import <Foundation/Foundation.h>

typedef unsigned int breakpoint_t;

@interface MacRubyDebuggerConnector : NSObject
{
    NSString *_interpreterPath;
    NSMutableArray *_arguments;
    NSString *_socketPath;
    NSTask *_task;
    NSFileHandle *_socket;
    NSString *_location;
}

- (id)initWithInterpreterPath:(NSString *)path arguments:(NSArray *)arguments;

// Execution control.

- (void)startExecution;
- (void)continueExecution;
- (void)stepExecution;
- (void)stopExecution;

// Current state.

- (NSString *)location;
- (NSArray *)localVariables;
- (NSArray *)backtrace;
- (void)setFrame:(unsigned int)frame;

// Breakpoints.

- (breakpoint_t)addBreakPointAtPath:(NSString *)path line:(unsigned int)line
    condition:(NSString *)condition;

- (void)enableBreakPoint:(breakpoint_t)bp;
- (void)disableBreakPoint:(breakpoint_t)bp;
- (void)deleteBreakPoint:(breakpoint_t)bp;
- (void)setCondition:(NSString *)condition forBreakPoint:(breakpoint_t)bp;

- (NSArray *)allBreakPoints;

// Context evaluation.

- (NSString *)evaluateExpression:(NSString *)expression;

@end
