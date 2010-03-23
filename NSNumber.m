/*
 * MacRuby extensions to NSNumber.
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

VALUE rb_cNSNumber;

@interface NSNumber (IsFloatExtension)
- (BOOL)isFloat;
@end

static id
get_nsnumber(VALUE obj)
{
    // TODO: handle bignums, #to_i / #to_f generic objects
    id num = RB2OC(obj);
    if (![num isKindOfClass:[NSNumber class]]) {
	rb_raise(rb_eTypeError, "excepted NSNumber-like object, got `%s'",
		[[[num class] description] UTF8String]);
    }
    return num;
}

static VALUE
get_numeric(id obj)
{
    return [obj isFloat]
	? DOUBLE2NUM([obj doubleValue]) : LONG2NUM([obj longValue]);
}

static id
nsnumber_to_s(id rcv, SEL sel)
{
    return [rcv stringValue];
}

static id
nsnumber_to_i(id rcv, SEL sel)
{
    if ([rcv isFloat]) {
	const long val = [rcv longValue];
	return [NSNumber numberWithLong:val];
    }
    return rcv;
}

static id
nsnumber_to_f(id rcv, SEL sel)
{
    if (![rcv isFloat]) {
	const double val = [rcv doubleValue];
	return [NSNumber numberWithDouble:val];
    }
    return rcv;
}

static VALUE
nsnumber_coerce(id rcv, SEL sel, VALUE other)
{
    id num = get_nsnumber(other);
    VALUE x = get_numeric(rcv);
    VALUE y = get_numeric(num);

    if (CLASS_OF(x) == CLASS_OF(y)) {
	return rb_assoc_new(y, x);	
    }
    return rb_assoc_new(rb_Float(y), rb_Float(x));
}

static id
nsnumber_uplus(id rcv, SEL sel)
{
    return rcv;
}

static id
nsnumber_uminus(id rcv, SEL sel)
{
    return [rcv isFloat]
	? [NSNumber numberWithDouble:-[rcv doubleValue]]
	: [NSNumber numberWithLong:-[rcv longValue]];
}

static id
nsnumber_plus(id rcv, SEL sel, VALUE other)
{
    id num = get_nsnumber(other);
    if ([rcv isFloat] || [num isFloat]) {
	return [NSNumber numberWithDouble:
	    [rcv doubleValue] + [num doubleValue]]; 
    }
    return [NSNumber numberWithLong:[rcv longValue] + [num longValue]];
}

static id
nsnumber_minus(id rcv, SEL sel, VALUE other)
{
    id num = get_nsnumber(other);
    if ([rcv isFloat] || [num isFloat]) {
	return [NSNumber numberWithDouble:
	    [rcv doubleValue] - [num doubleValue]]; 
    }
    return [NSNumber numberWithLong:[rcv longValue] - [num longValue]];
}

static id
nsnumber_mul(id rcv, SEL sel, VALUE other)
{
    id num = get_nsnumber(other);
    if ([rcv isFloat] || [num isFloat]) {
	return [NSNumber numberWithDouble:
	    [rcv doubleValue] * [num doubleValue]]; 
    }
    return [NSNumber numberWithLong:[rcv longValue] * [num longValue]];
}

static id
nsnumber_divide(id x, id y, bool fdiv)
{
    if (fdiv) {
	return [NSNumber numberWithDouble:
	    [x doubleValue] / [y doubleValue]]; 
    } 
    const long val = [y longValue];
    if (val == 0) {
	rb_raise(rb_eZeroDivError, "divided by 0");
    }
    return [NSNumber numberWithLong:[x longValue] / val];
}

static id
nsnumber_div(id rcv, SEL sel, VALUE other)
{
    id num = get_nsnumber(other);
    return nsnumber_divide(rcv, num, [rcv isFloat] || [num isFloat]);
}

static id
nsnumber_idiv(id rcv, SEL sel, VALUE other)
{
    id num = get_nsnumber(other);
    return nsnumber_divide(rcv, num, false);
}

static id
nsnumber_fdiv(id rcv, SEL sel, VALUE other)
{
    id num = get_nsnumber(other);
    return nsnumber_divide(rcv, num, true);
}

static id
nsnumber_mod(id rcv, SEL sel, VALUE other)
{
    id num = get_nsnumber(other);
    if ([rcv isFloat] || [num isFloat]) {
	return [NSNumber numberWithDouble:
	    fmod([rcv doubleValue], [num doubleValue])];
    }
    const long val = [num longValue];
    if (val == 0) {
	rb_raise(rb_eZeroDivError, "divided by 0");
    }
    return [NSNumber numberWithLong:[rcv longValue] % val];
}

static id
nsnumber_divmod(id rcv, SEL sel, VALUE other)
{
    id num = get_nsnumber(other);
    NSNumber *div, *mod;
    if ([rcv isFloat] || [num isFloat]) {
	div = [NSNumber numberWithDouble:
	    [rcv doubleValue] / [num doubleValue]];
	mod = [NSNumber numberWithDouble:
	    fmod([rcv doubleValue], [num doubleValue])];
    }
    else {
	const long val = [num longValue];
	if (val == 0) {
	    rb_raise(rb_eZeroDivError, "divided by 0");
	}
	div = [NSNumber numberWithLong:[rcv longValue] / val];
	mod = [NSNumber numberWithLong:[rcv longValue] % val];
    }
    return [NSArray arrayWithObjects:div, mod, NULL];
}

//static id
//nsnumber_pow(id rcv, SEL sel, VALUE other)
//{
//}

static id
nsnumber_abs(id rcv, SEL sel)
{
    if ([rcv isFloat]) {
	double val = [rcv doubleValue];
	if (val < 0) {
	    val = -val;
	    return [NSNumber numberWithDouble:val];
	}
	return rcv;
    }
    long val = [rcv longValue];
    if (val < 0) {
	val = -val;
	return [NSNumber numberWithLong:val];
    }
    return rcv;
}

static VALUE
nsnumber_equal(id rcv, SEL sel, VALUE other)
{
    return [rcv isEqualToNumber:get_nsnumber(other)] ? Qtrue : Qfalse;
}

static VALUE
nsnumber_cmp(id rcv, SEL sel, VALUE other)
{
    const int res = [rcv compare:get_nsnumber(other)];
    return res == 0
	? INT2FIX(0)
	: res < 0
	    ? INT2FIX(-1)
	    : INT2FIX(1);
}

static VALUE
nsnumber_gt(id rcv, SEL sel, VALUE other)
{
    const int res = [rcv compare:get_nsnumber(other)];
    return res > 0 ? Qtrue : Qfalse;
}

static VALUE
nsnumber_ge(id rcv, SEL sel, VALUE other)
{
    const int res = [rcv compare:get_nsnumber(other)];
    return res >= 0 ? Qtrue : Qfalse;
}

static VALUE
nsnumber_lt(id rcv, SEL sel, VALUE other)
{
    const int res = [rcv compare:get_nsnumber(other)];
    return res < 0 ? Qtrue : Qfalse;
}

static VALUE
nsnumber_le(id rcv, SEL sel, VALUE other)
{
    const int res = [rcv compare:get_nsnumber(other)];
    return res <= 0 ? Qtrue : Qfalse;
}

static void
assume_integral(id obj)
{
    if ([obj isFloat]) {
	rb_raise(rb_eTypeError, "expected integral (non-float) NSNumber");
    }
}

static id
nsnumber_rev(id rcv, SEL sel)
{
    assume_integral(rcv);
    long val = [rcv longValue];
    val = ~val;
    return [NSNumber numberWithLong:val];
}

static id
nsnumber_and(id rcv, SEL sel, VALUE other)
{
    assume_integral(rcv);
    long val = [rcv longValue] & [get_nsnumber(other) longValue];
    return [NSNumber numberWithLong:val];
}

static id
nsnumber_or(id rcv, SEL sel, VALUE other)
{
    assume_integral(rcv);
    long val = [rcv longValue] | [get_nsnumber(other) longValue];
    return [NSNumber numberWithLong:val];
}

static id
nsnumber_xor(id rcv, SEL sel, VALUE other)
{
    assume_integral(rcv);
    long val = [rcv longValue] ^ [get_nsnumber(other) longValue];
    return [NSNumber numberWithLong:val];
}

static VALUE
nsnumber_aref(id rcv, SEL sel, VALUE other)
{
    assume_integral(rcv);
    const long val = [rcv longValue];

    const long i = FIX2LONG(other);
    if (i < 0) {
	return INT2FIX(0);
    }
    if (SIZEOF_LONG * CHAR_BIT-1 < i) {
	if (val < 0) {
	    return INT2FIX(1);
	}
	return INT2FIX(0);
    }
    if (val & (1L << i)) {
	return INT2FIX(1);
    }
    return INT2FIX(0);
}

static VALUE
nsnumber_lshift(id rcv, SEL sel, VALUE other)
{
    assume_integral(rcv);
    long val = [rcv longValue];
    const long width = NUM2LONG(other);

    if (width < 0) {
	val = val >> -width;
    }
    else {
	val = val << width;
    }
    return LONG2NUM(val);
}

static VALUE
nsnumber_rshift(id rcv, SEL sel, VALUE other)
{
    assume_integral(rcv);
    long val = [rcv longValue];
    const long width = NUM2LONG(other);

    if (width < 0) {
	val = val << -width;
    }
    else {
	val = val >> width;
    }
    return LONG2NUM(val);
}

static void *
imp_nsnumber_to_int(void *rcv, SEL sel)
{
    // This is because some NSNumber subclasses must be converted to real
    // CFNumber objects, in order to be coerced into Fixnum objects.
    long val = 0;
    if (!CFNumberGetValue((CFNumberRef)rcv, kCFNumberLongType, &val)) {
	rb_raise(rb_eTypeError, "cannot get 'long' value out of NSNumber %p",
		rcv);
    }
    CFNumberRef new_num = CFNumberCreate(NULL, kCFNumberLongType, &val);
    CFMakeCollectable(new_num);
    return (void *)new_num;
}

void
Init_NSNumber(void)
{
    rb_cNSNumber = (VALUE)objc_getClass("NSNumber");
    assert(rb_cNSNumber != 0);

    rb_objc_define_method(rb_cNSNumber, "to_s", nsnumber_to_s, 0);
    rb_objc_define_method(rb_cNSNumber, "to_i", nsnumber_to_i, 0);
    rb_objc_define_method(rb_cNSNumber, "to_f", nsnumber_to_f, 0);
    rb_objc_define_method(rb_cNSNumber, "coerce", nsnumber_coerce, 1);
    rb_objc_define_method(rb_cNSNumber, "+@", nsnumber_uplus, 0);
    rb_objc_define_method(rb_cNSNumber, "-@", nsnumber_uminus, 0);
    rb_objc_define_method(rb_cNSNumber, "+", nsnumber_plus, 1);
    rb_objc_define_method(rb_cNSNumber, "-", nsnumber_minus, 1);
    rb_objc_define_method(rb_cNSNumber, "*", nsnumber_mul, 1);
    rb_objc_define_method(rb_cNSNumber, "/", nsnumber_div, 1);
    rb_objc_define_method(rb_cNSNumber, "div", nsnumber_idiv, 1);
    rb_objc_define_method(rb_cNSNumber, "%", nsnumber_mod, 1);
    rb_objc_define_method(rb_cNSNumber, "modulo", nsnumber_mod, 1);
    rb_objc_define_method(rb_cNSNumber, "divmod", nsnumber_divmod, 1);
    rb_objc_define_method(rb_cNSNumber, "fdiv", nsnumber_fdiv, 1);
    //rb_objc_define_method(rb_cNSNumber, "**", nsnumber_pow, 1);
    rb_objc_define_method(rb_cNSNumber, "abs", nsnumber_abs, 0);
    rb_objc_define_method(rb_cNSNumber, "==", nsnumber_equal, 1);
    rb_objc_define_method(rb_cNSNumber, "<=>", nsnumber_cmp, 1);
    rb_objc_define_method(rb_cNSNumber, ">",  nsnumber_gt, 1);
    rb_objc_define_method(rb_cNSNumber, ">=", nsnumber_ge, 1);
    rb_objc_define_method(rb_cNSNumber, "<",  nsnumber_lt, 1);
    rb_objc_define_method(rb_cNSNumber, "<=", nsnumber_le, 1);
    rb_objc_define_method(rb_cNSNumber, "~", nsnumber_rev, 0);
    rb_objc_define_method(rb_cNSNumber, "&", nsnumber_and, 1);
    rb_objc_define_method(rb_cNSNumber, "|", nsnumber_or,  1);
    rb_objc_define_method(rb_cNSNumber, "^", nsnumber_xor, 1);
    rb_objc_define_method(rb_cNSNumber, "[]", nsnumber_aref, 1);
    rb_objc_define_method(rb_cNSNumber, "<<", nsnumber_lshift, 1);
    rb_objc_define_method(rb_cNSNumber, ">>", nsnumber_rshift, 1);

    class_replaceMethod((Class)rb_cNSNumber, sel_registerName("to_int"),
	    (IMP)imp_nsnumber_to_int, "@@:");
}

@implementation NSNumber (IsFLoatExtension)
- (BOOL)isFloat
{
    return CFNumberIsFloatType((CFNumberRef)self);
}
@end
