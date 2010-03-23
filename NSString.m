/*
 * MacRuby extensions to NSString.
 * 
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2010, Apple Inc. All rights reserved.
 */

#import <Foundation/Foundation.h>

#include "ruby/ruby.h"
#include "ruby/node.h"
#include "objc.h"
#include "vm.h"
#include "encoding.h"

VALUE rb_cString;
VALUE rb_cNSString;
VALUE rb_cNSMutableString;

static id
nsstr_inspect(id rcv, SEL sel)
{
    return [NSString stringWithFormat:@"\"%@\"", rcv];
}

static id
nsstr_to_s(id rcv, SEL sel)
{
    return rcv;
}

void
Init_NSString(void)
{
    rb_cNSString = (VALUE)objc_getClass("NSString");
    assert(rb_cNSString != 0);
    rb_cString = rb_cNSString;
    rb_include_module(rb_cString, rb_mComparable);
    rb_cNSMutableString = (VALUE)objc_getClass("NSMutableString");
    assert(rb_cNSMutableString != 0);

    rb_objc_define_method(rb_cString, "inspect", nsstr_inspect, 0);
    rb_objc_define_method(rb_cString, "to_s", nsstr_to_s, 0);
}
