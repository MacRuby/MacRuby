/*
 * MacRuby extensions to NSString.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2010-2011, Apple Inc. All rights reserved.
 */

#import <Foundation/Foundation.h>

#include "macruby_internal.h"
#include "ruby/node.h"
#include "objc.h"
#include "vm.h"
#include "encoding.h"

VALUE rb_cString;
VALUE rb_cNSString;
VALUE rb_cNSMutableString;

// Some NSString instances actually do not even respond to mutable methods.
// So one way to know is to check if the setString: method exists.
#define CHECK_MUTABLE(obj) \
    do { \
        if (![obj respondsToSelector:@selector(setString:)]) { \
	    rb_raise(rb_eRuntimeError, \
		    "can't modify frozen/immutable string"); \
        } \
    } \
    while (0)

// If a given mutable operation raises an NSException error,
// it is likely that the object is not mutable.
#define TRY_MOP(code) \
    @try { \
	code; \
    } \
    @catch(NSException *exc) { \
	rb_raise(rb_eRuntimeError, "can't modify frozen/immutable string"); \
    }

static inline VALUE
to_str(VALUE str)
{
    switch (TYPE(str)) {
	case T_STRING:
	    return str;
	case T_SYMBOL:
	    return rb_sym_to_s(str);
    }
    return rb_convert_type(str, T_STRING, "String", "to_str");
}

static id
nsstr_dup(id rcv, SEL sel)
{
    if (TYPE(rcv) == T_SYMBOL) {
        rb_raise(rb_eTypeError, "can't dup %s", rb_obj_classname((VALUE)rcv));
    }

    id dup = (id)str_new_from_cfstring((CFStringRef)rcv);
    if (OBJ_TAINTED(rcv)) {
	OBJ_TAINT(dup);
    }
    return dup;
}

static id
nsstr_to_s(id rcv, SEL sel)
{
    return rcv;
}

static id
nsstr_replace(id rcv, SEL sel, VALUE other)
{
    CHECK_MUTABLE(rcv);
    TRY_MOP([rcv setString:(id)to_str(other)]);
    return rcv;
}

static id
nsstr_clear(id rcv, SEL sel)
{
    CHECK_MUTABLE(rcv);
    const long len = [rcv length];
    if (len > 0) {
	TRY_MOP([rcv deleteCharactersInRange:NSMakeRange(0, len)]);
    }
    return rcv;
}

static VALUE
nsstr_encoding(id rcv, SEL sel)
{
    // All NSStrings are Unicode, so let's return UTF-8.
    return (VALUE)rb_encodings[ENCODING_UTF8];
}

static VALUE
nsstr_length(id rcv, SEL sel)
{
    return LONG2NUM([rcv length]);
}

static VALUE
nsstr_empty(id rcv, SEL sel)
{
    return [rcv length] == 0 ? Qtrue : Qfalse;
}

static id
nsstr_concat(id rcv, SEL sel, VALUE other)
{
    CHECK_MUTABLE(rcv);
    TRY_MOP([rcv appendString:(id)to_str(other)]);
    return rcv;
}

static id
nsstr_plus(id rcv, SEL sel, VALUE other)
{
    id newstr = [NSMutableString new];
    [newstr appendString:rcv];
    [newstr appendString:(id)to_str(other)];
    return newstr;
}

static VALUE
nsstr_equal(id rcv, SEL sel, VALUE other)
{
    if (rcv == (id)other) {
        return Qtrue;
    }
    if (TYPE(other) != T_STRING) {
        if (!rb_respond_to(other, rb_intern("to_str"))) {
            return Qfalse;
        }
        return rb_equal(other, (VALUE)rcv);
    }
    return [rcv isEqualToString:(id)to_str(other)] ? Qtrue : Qfalse;
}

static VALUE
nsstr_cmp(id rcv, SEL sel, VALUE other)
{
    int ret = [rcv compare:(id)to_str(other)];
    if (ret > 0) {
	ret = 1;
    }
    else if (ret < 0) {
	ret = -1;
    }
    return INT2FIX(ret);
}

static VALUE
nsstr_casecmp(id rcv, SEL sel, VALUE other)
{
    int ret = [rcv compare:(id)to_str(other) options:NSCaseInsensitiveSearch];
    if (ret > 0) {
	ret = 1;
    }
    else if (ret < 0) {
	ret = -1;
    }
    return INT2FIX(ret);
}

static VALUE
nsstr_include(id rcv, SEL sel, VALUE other)
{
    NSRange range = [rcv rangeOfString:(id)to_str(other)];
    return range.location == NSNotFound ? Qfalse : Qtrue;
}

static VALUE
nsstr_to_rstr(id nsstr)
{
    const long len = [nsstr length];
    if (len == 0) {
	return rb_str_new(NULL, 0);
    }

    unichar *buf = (unichar *)malloc(sizeof(unichar) * len);
    [nsstr getCharacters:buf range:NSMakeRange(0, len)];
    VALUE rstr = rb_unicode_str_new(buf, len);
    free(buf);
    return rstr;
}

static VALUE
nsstr_forward_m1(id rcv, SEL sel, int argc, VALUE *argv)
{
    return rb_vm_call2(rb_vm_current_block(), nsstr_to_rstr(rcv), 0, sel, argc, argv);
}

static VALUE
nsstr_forward_0(id rcv, SEL sel)
{
    return nsstr_forward_m1(rcv, sel, 0, NULL);
}

static VALUE
nsstr_forward_1(id rcv, SEL sel, VALUE arg1)
{
    return nsstr_forward_m1(rcv, sel, 1, &arg1);
}

static VALUE
nsstr_forward_2(id rcv, SEL sel, VALUE arg1, VALUE arg2)
{
    VALUE args[2] = {arg1, arg2};
    return nsstr_forward_m1(rcv, sel, 2, args);
}

static VALUE
nsstr_forward_3(id rcv, SEL sel, VALUE arg1, VALUE arg2, VALUE arg3)
{
    VALUE args[3] = {arg1, arg2, arg3};
    return nsstr_forward_m1(rcv, sel, 3, args);
}

static VALUE
nsstr_forward_bang_m1(id rcv, SEL sel, int argc, VALUE *argv)
{
    CHECK_MUTABLE(rcv);
    VALUE rcv_rstr = nsstr_to_rstr(rcv);
    VALUE ret = rb_vm_call2(rb_vm_current_block(), rcv_rstr, 0, sel, argc, argv);
    TRY_MOP([rcv setString:(id)rcv_rstr]);
    return ret;
}

static VALUE
nsstr_forward_bang_0(id rcv, SEL sel)
{
    return nsstr_forward_bang_m1(rcv, sel, 0, NULL);
}

static VALUE
nsstr_forward_bang_1(id rcv, SEL sel, VALUE arg1)
{
    return nsstr_forward_bang_m1(rcv, sel, 1, &arg1);    
}

static VALUE
nsstr_forward_bang_2(id rcv, SEL sel, VALUE arg1, VALUE arg2)
{
    VALUE args[2] = {arg1, arg2};
    return nsstr_forward_bang_m1(rcv, sel, 2, args);
}

static VALUE
nsstr_forward_bang_3(id rcv, SEL sel, VALUE arg1, VALUE arg2, VALUE arg3)
{
    VALUE args[3] = {arg1, arg2, arg3};
    return nsstr_forward_bang_m1(rcv, sel, 3, args);
}

void
rb_str_NSCoder_encode(void *coder, VALUE str, const char *key)
{
    NSString *nskey = [NSString stringWithUTF8String:key];
    [(NSCoder *)coder encodeObject:(NSString *)str forKey:nskey];
}

VALUE
rb_str_NSCoder_decode(void *coder, const char *key)
{
    NSString *nskey = [NSString stringWithUTF8String:key];
    return OC2RB([(NSCoder *)coder decodeObjectForKey:nskey]);
}

void
Init_NSString(void)
{
    rb_cString = rb_cNSString;
    rb_include_module(rb_cString, rb_mComparable);
    rb_cNSMutableString = (VALUE)objc_getClass("NSMutableString");
    assert(rb_cNSMutableString != 0);

    rb_objc_define_method(rb_cString, "dup", nsstr_dup, 0);
    rb_objc_define_method(rb_cString, "to_s", nsstr_to_s, 0);
    rb_objc_define_method(rb_cString, "to_str", nsstr_to_s, 0);
    rb_objc_define_method(rb_cString, "replace", nsstr_replace, 1);
    rb_objc_define_method(rb_cString, "clear", nsstr_clear, 0);
    rb_objc_define_method(rb_cString, "encoding", nsstr_encoding, 0);
    rb_objc_define_method(rb_cString, "size", nsstr_length, 0);
    rb_objc_define_method(rb_cString, "empty?", nsstr_empty, 0);
    rb_objc_define_method(rb_cString, "<<", nsstr_concat, 1);
    rb_objc_define_method(rb_cString, "concat", nsstr_concat, 1);
    rb_objc_define_method(rb_cString, "+", nsstr_plus, 1);
    rb_objc_define_method(rb_cString, "==", nsstr_equal, 1);
    rb_objc_define_method(rb_cString, "eql?", nsstr_equal, 1);
    rb_objc_define_method(rb_cString, "<=>", nsstr_cmp, 1);
    rb_objc_define_method(rb_cString, "casecmp", nsstr_casecmp, 1);
    rb_objc_define_method(rb_cString, "include?", nsstr_include, 1);

#define pick_forwarder(arity, bang) \
    (arity == -1 \
	? (bang \
	    ? (void *)nsstr_forward_bang_m1 : (void *)nsstr_forward_m1)  \
     : (arity == 0) \
	? (bang \
	    ? (void *)nsstr_forward_bang_0 : (void *)nsstr_forward_0) \
     : (arity == 1) \
	? (bang \
	    ? (void *)nsstr_forward_bang_1 : (void *)nsstr_forward_1) \
     : (arity == 2) \
	? (bang \
	    ? (void *)nsstr_forward_bang_2 : (void *)nsstr_forward_2) \
     : (arity == 3) \
	? (bang \
	    ? (void *)nsstr_forward_bang_3 : (void *)nsstr_forward_3) \
     : (abort(),NULL))

#define forward(msg, arity) \
    rb_objc_define_method(rb_cString, msg, pick_forwarder(arity, false), arity)

#define forward_bang(msg, arity) \
    rb_objc_define_method(rb_cString, msg, pick_forwarder(arity, true), arity)
    
    // These methods are implemented as forwarders.
    forward("[]", -1);
    forward_bang("[]=", -1);
    forward("slice", -1);
    forward_bang("slice!", -1);
    forward_bang("insert", 2);
    forward("index", -1);
    forward("rindex", -1);
    forward("+", 1);
    forward("*", 1);
    forward("%", 1);
    forward("start_with?", -1);
    forward("end_with?", -1);
    forward("to_sym", 0);
    forward("intern", 0);
    forward("inspect", 0);
    forward("dump", 0);
    forward("match", -1);
    forward("=~", 1);
    forward("scan", 1);
    forward("split", -1);
    forward("to_i", -1);
    forward("hex", 0);
    forward("oct", 0);
    forward("ord", 0);
    forward("chr", 0);
    forward("to_f", 0);
    forward("chomp", -1);
    forward_bang("chomp!", -1);
    forward("chop", -1);
    forward_bang("chop!", -1);
    forward("sub", -1);
    forward_bang("sub!", -1);
    forward("gsub", -1);
    forward_bang("gsub!", -1);
    forward("downcase", 0);
    forward_bang("downcase!", 0);
    forward("upcase", 0);
    forward_bang("upcase!", 0);
    forward("swapcase", 0);
    forward_bang("swapcase!", 0);
    forward("capitalize", 0);
    forward_bang("capitalize!", 0);
    forward("ljust", -1);
    forward("rjust", -1);
    forward("center", -1);
    forward("strip", 0);
    forward("lstrip", 0);
    forward("rstrip", 0);
    forward_bang("strip!", 0);
    forward_bang("lstrip!", 0);
    forward_bang("rstrip!", 0);
    forward("lines", -1);
    forward("each_line", -1);
    forward("chars", 0);
    forward("each_char", 0);
    forward("succ", 0);
    forward_bang("succ!", 0);
    forward("next", 0);
    forward_bang("next!", 0);
    forward("upto", -1);
    forward("reverse", 0);
    forward_bang("reverse!", 0);
    forward("count", -1);
    forward("delete", -1);
    forward_bang("delete!", -1);
    forward("squeeze", -1);
    forward_bang("squeeze!", -1);
    forward("tr", 2);
    forward_bang("tr!", 2);
    forward("tr_s", 2);
    forward_bang("tr_s!", 2);
    forward("sum", -1);
    forward("partition", 1);
    forward("rpartition", 1);
    forward("crypt", 1);

#undef forward
#undef forward_bang

    // These methods will not work on NSStrings.
    rb_objc_define_method(rb_cString, "bytesize", rstr_only, 0);
    rb_objc_define_method(rb_cString, "getbyte", rstr_only, 1);
    rb_objc_define_method(rb_cString, "setbyte", rstr_only, 2);
    rb_objc_define_method(rb_cString, "force_encoding", rstr_only, 1);
    rb_objc_define_method(rb_cString, "valid_encoding?", rstr_only, 0);
    rb_objc_define_method(rb_cString, "ascii_only?", rstr_only, 0);
    rb_objc_define_method(rb_cString, "bytes", rstr_only, 0);
    rb_objc_define_method(rb_cString, "each_byte", rstr_only, 0);
    rb_objc_define_method(rb_cString, "to_data", rstr_only, 0);
    rb_objc_define_method(rb_cString, "pointer", rstr_only, 0);
}

const char *
nsstr_cstr(VALUE str)
{
    return [(NSString *)str UTF8String];
}

long
nsstr_clen(VALUE str)
{
    return [(NSString *)str lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
}
