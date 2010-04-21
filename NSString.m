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
    id dup = [rcv mutableCopy];
    if (OBJ_TAINTED(rcv)) {
	OBJ_TAINT(dup);
    }
    return dup;
}

static id
nsstr_clone(id rcv, SEL sel)
{
    id clone = nsstr_dup(rcv, 0);
    if (OBJ_FROZEN(rcv)) {
	OBJ_FREEZE(clone);
    }
    return clone;
}

static id
nsstr_to_s(id rcv, SEL sel)
{
    return rcv;
}

static id
nsstr_replace(id rcv, SEL sel, VALUE other)
{
    [rcv setString:(id)to_str(other)];
    return rcv;
}

static id
nsstr_clear(id rcv, SEL sel)
{
    const long len = [rcv length];
    if (len > 0) {
	[rcv deleteCharactersInRange:NSMakeRange(0, len)];
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
    [rcv appendString:(id)to_str(other)];
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

static id
rstr_only(id rcv, SEL sel)
{
    rb_raise(rb_eArgError, "method `%s' does not work on NSStrings",
	    sel_getName(sel));
    return rcv; // never reached
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
nsstr_forward(id rcv, SEL sel, int argc, VALUE *argv)
{
    return rb_vm_call_with_cache2(rb_vm_get_call_cache(sel),
	rb_vm_current_block(), nsstr_to_rstr(rcv), rb_cRubyString, sel, argc,
	argv);
}

static VALUE
nsstr_forward_bang(id rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE rcv_rstr = nsstr_to_rstr(rcv);
    VALUE ret = rb_vm_call_with_cache2(rb_vm_get_call_cache(sel),
	rb_vm_current_block(), rcv_rstr, rb_cRubyString, sel, argc, argv);
    [rcv setString:(id)rcv_rstr];
    return ret;
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
    rb_cNSString = (VALUE)objc_getClass("NSString");
    assert(rb_cNSString != 0);
    rb_cString = rb_cNSString;
    rb_include_module(rb_cString, rb_mComparable);
    rb_cNSMutableString = (VALUE)objc_getClass("NSMutableString");
    assert(rb_cNSMutableString != 0);

    rb_objc_define_method(rb_cString, "dup", nsstr_dup, 0);
    rb_objc_define_method(rb_cString, "clone", nsstr_clone, 0);
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

#define forward(msg) \
	rb_objc_define_method(rb_cString, msg, nsstr_forward, -1)
#define forward_bang(msg) \
	rb_objc_define_method(rb_cString, msg, nsstr_forward_bang, -1)
    
    // These methods are implemented as forwarders.
    forward("[]");
    forward_bang("[]=");
    forward("slice");
    forward_bang("slice!");
    forward_bang("insert");
    forward("index");
    forward("rindex");
    forward("+");
    forward("*");
    forward("%");
    forward("start_with?");
    forward("end_with?");
    forward("to_sym");
    forward("intern");
    forward("inspect");
    forward("dump");
    forward("match");
    forward("=~");
    forward("scan");
    forward("split");
    forward("to_i");
    forward("hex");
    forward("oct");
    forward("ord");
    forward("chr");
    forward("to_f");
    forward("chomp");
    forward_bang("chomp!");
    forward("chop");
    forward_bang("chop!");
    forward("sub");
    forward_bang("sub!");
    forward("gsub");
    forward_bang("gsub!");
    forward("downcase");
    forward_bang("downcase!");
    forward("upcase");
    forward_bang("upcase!");
    forward("swapcase");
    forward_bang("swapcase!");
    forward("capitalize");
    forward_bang("capitalize!");
    forward("ljust");
    forward("rjust");
    forward("center");
    forward("strip");
    forward("lstrip");
    forward("rstrip");
    forward_bang("strip!");
    forward_bang("lstrip!");
    forward_bang("rstrip!");
    forward("lines");
    forward("each_line");
    forward("chars");
    forward("each_char");
    forward("succ");
    forward_bang("succ!");
    forward("next");
    forward_bang("next!");
    forward("upto");
    forward("reverse");
    forward_bang("reverse!");
    forward("count");
    forward("delete");
    forward_bang("delete!");
    forward("squeeze");
    forward_bang("squeeze!");
    forward("tr");
    forward_bang("tr!");
    forward("tr_s");
    forward_bang("tr_s!");
    forward("sum");
    forward("partition");
    forward("rpartition");
    forward("crypt");

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
}
