/*
 * MacRuby extensions to NSArray.
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
#include "array.h"
#include "hash.h"

VALUE rb_cArray;
VALUE rb_cNSArray;
VALUE rb_cNSMutableArray;

// Some NSArray instances actually do not even respond to mutable methods.
// So one way to know is to check if the addObject: method exists.
#define CHECK_MUTABLE(obj) \
    do { \
        if (![obj respondsToSelector:@selector(addObject:)]) { \
	    rb_raise(rb_eRuntimeError, \
		    "can't modify frozen/immutable array"); \
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
	rb_raise(rb_eRuntimeError, "can't modify frozen/immutable array"); \
    }

static id
nsary_dup(id rcv, SEL sel)
{
    id dup = [rcv mutableCopy];
    if (OBJ_TAINTED(rcv)) {
	OBJ_TAINT(dup);
    }
    return dup;
}

static id
nsary_clear(id rcv, SEL sel)
{
    CHECK_MUTABLE(rcv);
    TRY_MOP([rcv removeAllObjects]);
    return rcv;
}

static id
nsary_inspect(id rcv, SEL sel)
{
    NSMutableString *str = [NSMutableString new];
    [str appendString:@"["];
    for (id item in rcv) {
	if ([str length] > 1) {
	    [str appendString:@", "];
	}
	[str appendString:(NSString *)rb_inspect(OC2RB(item))];
    }
    [str appendString:@"]"];
    return str;
}

static id
nsary_to_a(id rcv, SEL sel)
{
    return rcv;
}

static VALUE
nsary_equal(id rcv, SEL sel, VALUE other)
{
    if (TYPE(other) != T_ARRAY) {
	return Qfalse;
    }
    return [rcv isEqualToArray:(id)other] ? Qtrue : Qfalse;
}

static VALUE
nsary_subseq(id rcv, long beg, long len)
{
    if (beg < 0 || len < 0) {
	return Qnil;
    }
    const long n = [rcv count];
    if (beg > n) {
	return Qnil;
    }
    if (n < len || n < beg + len) {
	len = n - beg;
    }
    return (VALUE)[[rcv subarrayWithRange:NSMakeRange(beg, len)] mutableCopy];
}

static VALUE
nsary_entry(id rcv, long offset)
{
    const long n = [rcv count];
    if (n == 0) {
	return Qnil;
    }
    if (offset < 0) {
	offset += n;
    }
    if (offset < 0 || n <= offset) {
	return Qnil;
    }
    return OC2RB([rcv objectAtIndex:offset]);
}

static VALUE
nsary_aref(id rcv, SEL sel, int argc, VALUE *argv)
{
    long beg, len;
    if (argc == 2) {
	beg = NUM2LONG(argv[0]);
	len = NUM2LONG(argv[1]);
	if (beg < 0) {
	    beg += [rcv count];
	}
	return nsary_subseq(rcv, beg, len);
    }
    if (argc != 1) {
	rb_scan_args(argc, argv, "11", 0, 0);
    }
    VALUE arg = argv[0];
    if (FIXNUM_P(arg)) {
	return nsary_entry(rcv, FIX2LONG(arg));
    }
    // Check if Range.
    switch (rb_range_beg_len(arg, &beg, &len, [rcv count], 0)) {
	case Qfalse:
	    break;
	case Qnil:
	    return Qnil;
	default:
	    return nsary_subseq(rcv, beg, len);
    }
    return nsary_entry(rcv, NUM2LONG(arg));
}

static void
nsary_splice(id ary, long beg, long len, VALUE rpl)
{
    const long n = [ary count];
    if (len < 0) {
	rb_raise(rb_eIndexError, "negative length (%ld)", len);
    }
    if (beg < 0) {
	beg += n;
	if (beg < 0) {
	    beg -= n;
	    rb_raise(rb_eIndexError, "index %ld out of array", beg);
	}
    }
    if (n < len || n < beg + len) {
	len = n - beg;
    }

    long rlen = 0;
    if (rpl != Qundef) {
	rpl = rb_ary_to_ary(rpl);
	rlen = RARRAY_LEN(rpl);
    }

    CHECK_MUTABLE(ary);

    if (beg >= n) {
	for (long i = n; i < beg; i++) {
	    TRY_MOP([ary addObject:[NSNull null]]);
	}
	if (rlen > 0 && rpl != Qundef) {
	    TRY_MOP([ary addObjectsFromArray:(id)rpl]);
	}
    }
    else {
	if (rlen > 0 && rpl != Qundef) {
	    TRY_MOP([ary replaceObjectsInRange:NSMakeRange(beg, len)
		withObjectsFromArray:(id)rpl]);
	}
	else {
	    TRY_MOP([ary removeObjectsInRange:NSMakeRange(beg, len)]);
	}
    }
}

static void
nsary_store(id ary, long idx, VALUE val)
{
    const long len = [ary count];
    if (idx < 0) {
	idx += len;
	if (idx < 0) {
	    rb_raise(rb_eIndexError, "index %ld out of array",
		    idx - len);
	}
    }
    else if (idx >= ARY_MAX_SIZE) {
	rb_raise(rb_eIndexError, "index %ld too big", idx);
    }

    CHECK_MUTABLE(ary);

    if (len <= idx) {
	for (long i = len; i <= idx; i++) {
	    TRY_MOP([ary addObject:[NSNull null]]);
	} 
    }	
    TRY_MOP([ary replaceObjectAtIndex:idx withObject:RB2OC(val)]);
}

static VALUE
nsary_aset(id rcv, SEL sel, int argc, VALUE *argv)
{
    if (argc == 3) {
	nsary_splice(rcv, NUM2LONG(argv[0]), NUM2LONG(argv[1]), argv[2]);
	return argv[2];
    }
    if (argc != 2) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 2)", argc);
    }

    long offset;
    if (FIXNUM_P(argv[0])) {
	offset = FIX2LONG(argv[0]);
    }
    else {
	long beg, len;
	if (rb_range_beg_len(argv[0], &beg, &len, [rcv count], 1)) {
	    // Check if Range.
	    nsary_splice(rcv, beg, len, argv[1]);
	    return argv[1];
	}
	offset = NUM2LONG(argv[0]);
    }
    nsary_store(rcv, offset, argv[1]);
    return argv[1];
}

static VALUE
nsary_at(id rcv, SEL sel, VALUE pos)
{
    return nsary_entry(rcv, NUM2LONG(pos));
}

static VALUE
nsary_fetch(id rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE pos, ifnone;
    rb_scan_args(argc, argv, "11", &pos, &ifnone);

    const bool block_given = rb_block_given_p();
    if (block_given && argc == 2) {
	rb_warn("block supersedes default value argument");
    }

    const long len = [rcv count];
    long idx = NUM2LONG(pos);
    if (idx < 0) {
	idx += len;
    }
    if (idx < 0 || len <= idx) {
	if (block_given) {
	    return rb_yield(pos);
	}
	if (argc == 1) {
	    rb_raise(rb_eIndexError, "index %ld out of array", idx);
	}
	return ifnone;
    }
    return OC2RB([rcv objectAtIndex:idx]);
}

static VALUE
nsary_shared_first(int argc, VALUE *argv, id ary, bool last, bool remove)
{
    VALUE nv;
    rb_scan_args(argc, argv, "1", &nv);
    long n = NUM2LONG(nv);

    const long ary_len = [ary count];
    if (n > ary_len) {
	n = ary_len;
    }
    else if (n < 0) {
	rb_raise(rb_eArgError, "negative array size");
    }

    long offset = 0;
    if (last) {
	offset = ary_len - n;
    }
    id result = [NSMutableArray new];

    for (long i = 0; i < n; i++) {
	id item = [ary objectAtIndex:i + offset];
	[result addObject:item];
    }

    if (remove) {
	CHECK_MUTABLE(ary);
	for (long i = 0; i < n; i++) {
	    TRY_MOP([ary removeObjectAtIndex:offset]);
	}
    }

    return (VALUE)result;
}

static VALUE
nsary_first(id rcv, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {
	return [rcv count] == 0 ? Qnil : OC2RB([rcv objectAtIndex:0]);
    }
    return nsary_shared_first(argc, argv, rcv, false, false);

}

static VALUE
nsary_last(id rcv, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {
	const long len = [rcv count];
	return len == 0 ? Qnil : OC2RB([rcv objectAtIndex:len - 1]);
    }
    return nsary_shared_first(argc, argv, rcv, true, false);
}

static id
nsary_concat(id rcv, SEL sel, VALUE ary)
{
    ary = to_ary(ary);
    CHECK_MUTABLE(rcv);
    TRY_MOP([rcv addObjectsFromArray:(id)ary]);
    return rcv;
}

static id
nsary_push(id rcv, SEL sel, VALUE elem)
{
    CHECK_MUTABLE(rcv);
    TRY_MOP([rcv addObject:RB2OC(elem)]);
    return rcv;
}

static id
nsary_push_m(id rcv, SEL sel, int argc, VALUE *argv)
{
    for (int i = 0; i < argc; i++) {
	nsary_push(rcv, 0, argv[i]);
    }
    return rcv;
}

static VALUE
nsary_pop(id rcv, SEL sel, int argc, VALUE *argv)
{
    CHECK_MUTABLE(rcv);
    if (argc == 0) {
	const long len = [rcv count];
	if (len > 0) {
	    id elem = [rcv objectAtIndex:len - 1];
	    TRY_MOP([rcv removeObjectAtIndex:len - 1]);
	    return OC2RB(elem);
	}
	return Qnil;
    }
    return nsary_shared_first(argc, argv, rcv, true, true);
}

static VALUE
nsary_shift(id rcv, SEL sel, int argc, VALUE *argv)
{
    CHECK_MUTABLE(rcv);
    if (argc == 0) {
	const long len = [rcv count];
	if (len > 0) {
	    id elem = [rcv objectAtIndex:0];
	    TRY_MOP([rcv removeObjectAtIndex:0]);
	    return OC2RB(elem);
	}
	return Qnil;
    }
    return nsary_shared_first(argc, argv, rcv, false, true);
}

static id
nsary_unshift(id rcv, SEL sel, int argc, VALUE *argv)
{
    CHECK_MUTABLE(rcv);
    for (int i = argc - 1; i >= 0; i--) {
	TRY_MOP([rcv insertObject:RB2OC(argv[i]) atIndex:0]);
    }
    return rcv;
}

static void
nsary_insert(id rcv, long idx, VALUE elem)
{
    CHECK_MUTABLE(rcv);
    const long len = [rcv count];
    if (idx < 0) {
	idx += len;
	if (idx < 0) {
	    rb_raise(rb_eIndexError, "index %ld out of array", idx - len);
	}
    }
    if (idx > len) {
	for (long i = len; i < idx; i++) {
	    TRY_MOP([rcv addObject:[NSNull null]]);
	} 	
    }
    TRY_MOP([rcv insertObject:RB2OC(elem) atIndex:idx]);
}

static id
nsary_insert_m(id rcv, SEL sel, int argc, VALUE *argv)
{
    CHECK_MUTABLE(rcv);
    if (argc < 1) {
	rb_raise(rb_eArgError, "wrong number of arguments (at least 1)");
    }
    if (argc > 1) {
	long pos = NUM2LONG(argv[0]);
	if (pos == -1) {
	    pos = [rcv count];
	}
	if (pos < 0) {
	    pos++;
	}
	if (argc == 2) {
	    nsary_insert(rcv, pos, argv[1]);
	}
	else {
	    argc--;
	    argv++;
	    NSMutableArray *rpl = [NSMutableArray new];
	    for (int i = 0; i < argc; i++) {
		[rpl addObject:RB2OC(argv[i])];
	    }
	    nsary_splice(rcv, pos, 0, (VALUE)rpl);
	}
    }
    return rcv;
}

static VALUE
nsary_each(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    for (long i = 0; i < [rcv count]; i++) {
	rb_yield(OC2RB([rcv objectAtIndex:i]));
	RETURN_IF_BROKEN();
    }
    return (VALUE)rcv;
}

static VALUE
nsary_each_index(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    for (long i = 0; i < [rcv count]; i++) {
	rb_yield(LONG2NUM(i));
	RETURN_IF_BROKEN();
    }
    return (VALUE)rcv;
}

static VALUE
nsary_reverse_each(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    long len = [rcv count];
    while (len--) {
	rb_yield(OC2RB([rcv objectAtIndex:len]));
	RETURN_IF_BROKEN();
	const long n = [rcv count];
	if (n < len) {
	    // Array was modified.
	    len = n;
	}
    }
    return (VALUE)rcv;
}

static VALUE
nsary_length(id rcv, SEL sel)
{
    return LONG2NUM([rcv count]);
}

static VALUE
nsary_empty(id rcv, SEL sel)
{
    return [rcv count] == 0 ? Qtrue : Qfalse;
}

static VALUE
nsary_index(id rcv, SEL sel, int argc, VALUE *argv)
{
    const long len = [rcv count];

    if (argc == 0) {
	RETURN_ENUMERATOR(rcv, 0, 0);
	for (long i = 0; i < len; i++) {
	    VALUE test = rb_yield(OC2RB([rcv objectAtIndex:i]));
	    RETURN_IF_BROKEN();
	    if (RTEST(test)) {
		return LONG2NUM(i);
	    }
	}
    }
    else {
	VALUE val;
	rb_scan_args(argc, argv, "01", &val);
	if (len > 0) {
	    NSUInteger index = [rcv indexOfObject:RB2OC(val)
		inRange:NSMakeRange(0, len)];
	    if (index != NSNotFound) {
		return LONG2NUM(index);
	    }
	}
    }
    return Qnil;
}

static VALUE
nsary_rindex(id rcv, SEL sel, int argc, VALUE *argv)
{
    const long len = [rcv count];

    if (argc == 0) {
	RETURN_ENUMERATOR(rcv, 0, 0);
	long i = len;
	while (i-- > 0) {
	    VALUE test = rb_yield(OC2RB([rcv objectAtIndex:i]));
	    RETURN_IF_BROKEN();
	    if (RTEST(test)) {
		return LONG2NUM(i);
	    }
	    const long n = [rcv count];
	    if (n < len) {
		// Array was modified.
		i = n;
	    }
	}
    }
    else {
	VALUE val;
 	rb_scan_args(argc, argv, "01", &val);
	id ocval = RB2OC(val);
	for (long i = len - 1; i >= 0; i--) {
	    id item = [rcv objectAtIndex:i];
	    if ([ocval isEqual:item]) {
		return LONG2NUM(i);
	    }
	}
    }
    return Qnil;
}

static id
nsary_reverse_bang(id rcv, SEL sel)
{
    CHECK_MUTABLE(rcv);
    for (long i = 0, count = [rcv count]; i < (count / 2); i++) {
	TRY_MOP([rcv exchangeObjectAtIndex:i withObjectAtIndex:count - i - 1]);
    }
    return rcv;
}

static id
nsary_reverse(id rcv, SEL sel)
{
    NSMutableArray *result = [NSMutableArray arrayWithArray:rcv];
    return nsary_reverse_bang(result, 0);
}

static NSInteger
sort_block(id x, id y, void *context)
{
    VALUE rbx = OC2RB(x);
    VALUE rby = OC2RB(y);
    VALUE ret = rb_yield_values(2, rbx, rby);
    return rb_cmpint(ret, rbx, rby);
}

static id
nsary_sort_bang(id rcv, SEL sel)
{
    CHECK_MUTABLE(rcv);
    if ([rcv count] > 1) {
	if (rb_block_given_p()) {
	    TRY_MOP([rcv sortUsingFunction:sort_block context:NULL]);
	}
	else {
	    TRY_MOP([rcv sortUsingSelector:@selector(compare:)]);
	}
    }
    return rcv;
}

static id
nsary_sort(id rcv, SEL sel)
{
    NSMutableArray *result = [NSMutableArray arrayWithArray:rcv];
    return nsary_sort_bang(result, 0);
}

static VALUE
nsary_sort_by_i(VALUE i)
{
    return rb_yield(i);
}

static VALUE
nsary_sort_by_bang(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);

    CHECK_MUTABLE(rcv);
    VALUE sorted = rb_objc_block_call((VALUE)rcv, sel_registerName("sort_by"), 0, 0,
	    nsary_sort_by_i, 0);
    TRY_MOP([rcv setArray:(id)sorted]);

    return (VALUE)rcv;
}

static VALUE
collect(id rcv)
{
    CHECK_MUTABLE(rcv);
    for (long i = 0, count = [rcv count]; i < count; i++) {
	id elem = [rcv objectAtIndex:i];
	id newval = RB2OC(rb_yield(OC2RB(elem)));
	RETURN_IF_BROKEN();
	TRY_MOP([rcv replaceObjectAtIndex:i withObject:newval]);
    }
    return (VALUE)rcv;
}

static VALUE
nsary_collect_bang(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    return collect(rcv);
}

static VALUE
nsary_collect(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    NSMutableArray *result = [NSMutableArray arrayWithArray:rcv];
    return collect(result);
}

static VALUE
nsary_select(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    NSMutableArray *result = [NSMutableArray new];
    for (long i = 0; i < [rcv count]; i++) {
	id elem = [rcv objectAtIndex:i];
	VALUE test = rb_yield(OC2RB(elem));
	RETURN_IF_BROKEN();
	if (RTEST(test)) {
	    [result addObject:elem];
	}
    }
    return (VALUE)result;
}

static VALUE
nsary_select_bang(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    CHECK_MUTABLE(rcv);
    NSMutableArray *result = [NSMutableArray new];
    for (long i = 0; i < [rcv count]; i++) {
	id elem = [rcv objectAtIndex:i];
	VALUE test = rb_yield(OC2RB(elem));
	RETURN_IF_BROKEN();
	if (!RTEST(test)) {
	    continue;
	}
	[result addObject:elem];
    }
    if ([result count] == [rcv count]) {
	return Qnil;
    }
    [rcv setArray:result];
    return (VALUE)rcv;
}

static VALUE
nsary_keep_if(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    nsary_select_bang(rcv, 0);
    return (VALUE)rcv;
}

static id
nsary_values_at(id rcv, SEL sel, int argc, VALUE *argv)
{
    const long rcvlen = [rcv count];
    NSMutableArray *result = [NSMutableArray new];
    for (long i = 0; i < argc; i++) {
	long beg, len;
	if (FIXNUM_P(argv[i])) {
	    id entry = (id)nsary_entry(rcv, FIX2LONG(argv[i]));
	    [result addObject:RB2OC(entry)];
	    continue;
	}
	switch (rb_range_beg_len(argv[i], &beg, &len, rcvlen, 0)) {
	    // Check if Range.
	    case Qfalse:
		break;
	    case Qnil:
		continue;
	    default:
		for (long j = 0; j < len; j++) {
		    [result addObject:[rcv objectAtIndex:j + beg]];
		}
		continue;
	}
	[result addObject:[rcv objectAtIndex:NUM2LONG(argv[i])]];
    }
    return result;
}

static bool
nsary_delete_element(id rcv, VALUE elem)
{
    id ocelem = RB2OC(elem);
    long len = [rcv count];
    NSRange range = NSMakeRange(0, len);
    NSUInteger index;
    bool changed = false;
    CHECK_MUTABLE(rcv);
    while ((index = [rcv indexOfObject:ocelem inRange:range]) != NSNotFound) {
	TRY_MOP([rcv removeObjectAtIndex:index]);
	range.location = index;
	range.length = --len - index;
	changed = true;
    }
    return changed;
}

static VALUE
nsary_delete(id rcv, SEL sel, VALUE elem)
{
    if (!nsary_delete_element(rcv, elem)) {
	if (rb_block_given_p()) {
	    return rb_yield(elem);
	}
	return Qnil;
    }
    return elem;
}

static VALUE
nsary_delete_at(id rcv, SEL sel, VALUE pos)
{
    CHECK_MUTABLE(rcv);
    long index = NUM2LONG(pos);
    const long len = [rcv count];
    if (index >= len) {
	return Qnil;
    }
    if (index < 0) {
	index += len;
	if (index < 0) {
	    return Qnil;
	}
    }
    VALUE elem = OC2RB([rcv objectAtIndex:index]);
    TRY_MOP([rcv removeObjectAtIndex:index]); 
    return elem;
}

static VALUE
reject(id rcv)
{
    CHECK_MUTABLE(rcv);
    bool changed = false;
    for (long i = 0, n = [rcv count]; i < n; i++) {
	VALUE elem = OC2RB([rcv objectAtIndex:i]);
	VALUE test = rb_yield(elem);
	RETURN_IF_BROKEN();
	if (RTEST(test)) {
	    TRY_MOP([rcv removeObjectAtIndex:i]);
	    n--;
	    i--;
	    changed = true;
	}
    }
    return changed ? (VALUE)rcv : Qnil;
}

static VALUE
nsary_delete_if(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    reject(rcv);
    return (VALUE)rcv;
}

static VALUE
nsary_reject(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    NSMutableArray *result = [NSMutableArray arrayWithArray:rcv];
    reject(result);
    return (VALUE)result;
}

static VALUE
nsary_reject_bang(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    return reject(rcv);
}

static id
nsary_replace(id rcv, SEL sel, VALUE other)
{
    other = to_ary(other);
    CHECK_MUTABLE(rcv);
    TRY_MOP([rcv setArray:(id)other]);
    return rcv;
}

static VALUE
nsary_includes(id rcv, SEL sel, VALUE obj)
{
    id elem = RB2OC(obj);
    return [rcv containsObject:elem] ? Qtrue : Qfalse;
}

static VALUE
nsary_slice_bang(id rcv, SEL sel, int argc, VALUE *argv)
{
    const long rcvlen = [rcv count];
    VALUE arg1, arg2;
    long pos, len;

    if (rb_scan_args(argc, argv, "11", &arg1, &arg2) == 2) {
	pos = NUM2LONG(arg1);
	len = NUM2LONG(arg2);
delete_pos_len:
	if (pos < 0) {
	    pos = rcvlen + pos;
	    if (pos < 0) {
		return Qnil;
	    }
	}
	if (rcvlen < len || rcvlen < pos + len) {
	    len = rcvlen - pos;
	}
	VALUE result = nsary_subseq(rcv, pos, len);
	nsary_splice(rcv, pos, len, Qundef);
	return result;
    }

    if (!FIXNUM_P(arg1)) {
	switch (rb_range_beg_len(arg1, &pos, &len, rcvlen, 0)) {
	    case Qtrue:
		// Valid range.
		goto delete_pos_len;
	    case Qnil:
		// invalid range.
		return Qnil;
	    default:
		// Not a range.
		break;
	}
    }

    return nsary_delete_at(rcv, 0, arg1);
}

static id
nsary_plus(id rcv, SEL sel, VALUE other)
{
    other = to_ary(other);
    NSMutableArray *ary = [NSMutableArray new];
    [ary addObjectsFromArray:rcv];
    [ary addObjectsFromArray:(id)other];
    return ary;
}

static id
nsary_times(id rcv, SEL sel, VALUE times)
{
    VALUE tmp = rb_check_string_type(times);
    if (!NIL_P(tmp)) {
	return (id)rb_ary_join((VALUE)rcv, tmp);
    }

    const long len = NUM2LONG(times);
    if (len < 0) {
	rb_raise(rb_eArgError, "negative argument");
    }

    NSMutableArray *result = [NSMutableArray new];
    if (len > 0) {
	const long n = [rcv count];
	if (LONG_MAX/len < n) {
	    rb_raise(rb_eArgError, "argument too big");
	}
	for (long i = 0; i < len; i++) {
	    [result addObjectsFromArray:rcv];
	}
    }
    return result;
}

static int
nsary_push_value(st_data_t key, st_data_t val, st_data_t ary)
{
    rb_ary_push((VALUE)ary, (VALUE)val);
    return ST_CONTINUE;
}

static VALUE
nsary_uniq_bang(id rcv, SEL sel)
{
    CHECK_MUTABLE(rcv);
    VALUE hash;
    long len = [rcv count];
    bool changed = false;
    if (len <= 1) {
	return Qnil;
    }

    NSMutableArray *result = [NSMutableArray new];
    if (rb_block_given_p()) {
	hash = rb_ary_make_hash_by((VALUE)rcv);
	if (len == RHASH_SIZE(hash)) {
	    return Qnil;
	}
	st_foreach(RHASH_TBL(hash), nsary_push_value, (st_data_t)result);
	changed = true;
    }
    else {
	hash = rb_ary_make_hash((VALUE)rcv, 0);
	if (len == RHASH_SIZE(hash)) {
	    return Qnil;
	}
	for (long i = 0; i < len; i++) {
	    id elem = [rcv objectAtIndex:i];
	    st_data_t vv = (st_data_t)OC2RB(elem);
	    if (st_delete(RHASH_TBL(hash), &vv, 0)) {
		[result addObject:elem];
		changed = true;
	    }
	}
    }

    if (!changed) {
	return Qnil;
    }
    [rcv setArray:result];
    return (VALUE)rcv;
}

static id 
nsary_uniq(id rcv, SEL sel)
{
    id result = [NSMutableArray arrayWithArray:rcv];
    nsary_uniq_bang(result, 0);
    return result;
}

static VALUE
nsary_compact_bang(id rcv, SEL sel)
{
    return nsary_delete_element(rcv, Qnil) ? (VALUE)rcv : Qnil;
}

static id
nsary_compact(id rcv, SEL sel)
{
    id result = [NSMutableArray arrayWithArray:rcv];
    nsary_compact_bang(result, 0);
    return result;
}

static VALUE
nsary_drop(id rcv, SEL sel, VALUE n)
{
    const long pos = NUM2LONG(n);
    if (pos < 0) {
	rb_raise(rb_eArgError, "attempt to drop negative size");
    }
    return nsary_subseq(rcv, pos, [rcv count]);
}

static VALUE
nsary_drop_while(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    long i = 0;
    for (long count = [rcv count]; i < count; i++) {
	VALUE v = rb_yield(OC2RB([rcv objectAtIndex:i]));
	RETURN_IF_BROKEN();
	if (!RTEST(v)) {
	    break;
	}
    }
    return nsary_drop(rcv, 0, LONG2FIX(i));
}

static VALUE
nsary_take(id rcv, SEL sel, VALUE n)
{
    const long len = NUM2LONG(n);
    if (len < 0) {
	rb_raise(rb_eArgError, "attempt to take negative size");
    }
    return nsary_subseq(rcv, 0, len);
}

static VALUE
nsary_take_while(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    long i = 0;
    for (long count = [rcv count]; i < count; i++) {
	VALUE v = rb_yield(OC2RB([rcv objectAtIndex:i]));
	RETURN_IF_BROKEN();
	if (!RTEST(v)) {
	    break;
	}
    }
    return nsary_take(rcv, 0, LONG2FIX(i));
}

static id
nsary_shuffle_bang(id rcv, SEL sel)
{
    CHECK_MUTABLE(rcv);
    long i = [rcv count];
    while (i > 0) {
	const long j = rb_genrand_real() * i--;
	TRY_MOP([rcv exchangeObjectAtIndex:i withObjectAtIndex:j]);
    }
    return rcv;
}

static id
nsary_shuffle(id rcv, SEL sel)
{
    id result = [NSMutableArray arrayWithArray:rcv];
    nsary_shuffle_bang(result, 0);
    return result;
}

void
Init_NSArray(void)
{
    rb_cArray = rb_cNSArray;
    rb_cNSMutableArray = (VALUE)objc_getClass("NSMutableArray");
    assert(rb_cNSMutableArray != 0);

    rb_include_module(rb_cArray, rb_mEnumerable);

    rb_objc_define_method(rb_cArray, "dup", nsary_dup, 0);
    rb_objc_define_method(rb_cArray, "clear", nsary_clear, 0);
    rb_objc_define_method(rb_cArray, "to_s", nsary_inspect, 0);
    rb_objc_define_method(rb_cArray, "inspect", nsary_inspect, 0);
    rb_objc_define_method(rb_cArray, "to_a", nsary_to_a, 0);
    rb_objc_define_method(rb_cArray, "to_ary", nsary_to_a, 0);
    rb_objc_define_method(rb_cArray, "==", nsary_equal, 1);
    rb_objc_define_method(rb_cArray, "eql?", nsary_equal, 1);
    rb_objc_define_method(rb_cArray, "[]", nsary_aref, -1);
    rb_objc_define_method(rb_cArray, "[]=", nsary_aset, -1);
    rb_objc_define_method(rb_cArray, "at", nsary_at, 1);
    rb_objc_define_method(rb_cArray, "fetch", nsary_fetch, -1);
    rb_objc_define_method(rb_cArray, "first", nsary_first, -1);
    rb_objc_define_method(rb_cArray, "last", nsary_last, -1);
    rb_objc_define_method(rb_cArray, "concat", nsary_concat, 1);
    rb_objc_define_method(rb_cArray, "<<", nsary_push, 1);
    rb_objc_define_method(rb_cArray, "push", nsary_push_m, -1);
    rb_objc_define_method(rb_cArray, "pop", nsary_pop, -1);
    rb_objc_define_method(rb_cArray, "shift", nsary_shift, -1);
    rb_objc_define_method(rb_cArray, "unshift", nsary_unshift, -1);
    rb_objc_define_method(rb_cArray, "insert", nsary_insert_m, -1);
    rb_objc_define_method(rb_cArray, "each", nsary_each, 0);
    rb_objc_define_method(rb_cArray, "each_index", nsary_each_index, 0);
    rb_objc_define_method(rb_cArray, "reverse_each", nsary_reverse_each, 0);
    rb_objc_define_method(rb_cArray, "length", nsary_length, 0);
    rb_objc_define_method(rb_cArray, "size", nsary_length, 0);
    rb_objc_define_method(rb_cArray, "empty?", nsary_empty, 0);
    rb_objc_define_method(rb_cArray, "find_index", nsary_index, -1);
    rb_objc_define_method(rb_cArray, "index", nsary_index, -1);
    rb_objc_define_method(rb_cArray, "rindex", nsary_rindex, -1);
    rb_objc_define_method(rb_cArray, "reverse", nsary_reverse, 0);
    rb_objc_define_method(rb_cArray, "reverse!", nsary_reverse_bang, 0);
    rb_objc_define_method(rb_cArray, "sort", nsary_sort, 0);
    rb_objc_define_method(rb_cArray, "sort!", nsary_sort_bang, 0);
    rb_objc_define_method(rb_cArray, "sort_by!", nsary_sort_by_bang, 0);
    rb_objc_define_method(rb_cArray, "collect", nsary_collect, 0);
    rb_objc_define_method(rb_cArray, "collect!", nsary_collect_bang, 0);
    rb_objc_define_method(rb_cArray, "map", nsary_collect, 0);
    rb_objc_define_method(rb_cArray, "map!", nsary_collect_bang, 0);
    rb_objc_define_method(rb_cArray, "select", nsary_select, 0);
    rb_objc_define_method(rb_cArray, "select!", nsary_select_bang, 0);
    rb_objc_define_method(rb_cArray, "keep_if", nsary_keep_if, 0);
    rb_objc_define_method(rb_cArray, "values_at", nsary_values_at, -1);
    rb_objc_define_method(rb_cArray, "delete", nsary_delete, 1);
    rb_objc_define_method(rb_cArray, "delete_at", nsary_delete_at, 1);
    rb_objc_define_method(rb_cArray, "delete_if", nsary_delete_if, 0);
    rb_objc_define_method(rb_cArray, "reject", nsary_reject, 0);
    rb_objc_define_method(rb_cArray, "reject!", nsary_reject_bang, 0);
    rb_objc_define_method(rb_cArray, "replace", nsary_replace, 1);
    rb_objc_define_method(rb_cArray, "include?", nsary_includes, 1);
    rb_objc_define_method(rb_cArray, "slice", nsary_aref, -1);
    rb_objc_define_method(rb_cArray, "slice!", nsary_slice_bang, -1);
    rb_objc_define_method(rb_cArray, "+", nsary_plus, 1);
    rb_objc_define_method(rb_cArray, "*", nsary_times, 1);
    rb_objc_define_method(rb_cArray, "uniq", nsary_uniq, 0);
    rb_objc_define_method(rb_cArray, "uniq!", nsary_uniq_bang, 0);
    rb_objc_define_method(rb_cArray, "compact", nsary_compact, 0);
    rb_objc_define_method(rb_cArray, "compact!", nsary_compact_bang, 0);
    rb_objc_define_method(rb_cArray, "shuffle!", nsary_shuffle_bang, 0);
    rb_objc_define_method(rb_cArray, "shuffle", nsary_shuffle, 0);
    rb_objc_define_method(rb_cArray, "take", nsary_take, 1);
    rb_objc_define_method(rb_cArray, "take_while", nsary_take_while, 0);
    rb_objc_define_method(rb_cArray, "drop", nsary_drop, 1);
    rb_objc_define_method(rb_cArray, "drop_while", nsary_drop_while, 0);

    // Implementation shared with RubyArray (and defined in array.c).
    rb_objc_define_method(rb_cArray, "-", rary_diff, 1);
    rb_objc_define_method(rb_cArray, "&", rary_and, 1);
    rb_objc_define_method(rb_cArray, "|", rary_or, 1);
    rb_objc_define_method(rb_cArray, "join", rary_join, -1);
    rb_objc_define_method(rb_cArray, "hash", rary_hash, 0);
    rb_objc_define_method(rb_cArray, "zip", rary_zip, -1);
    rb_objc_define_method(rb_cArray, "transpose", rary_transpose, 0);
    rb_objc_define_method(rb_cArray, "fill", rary_fill, -1);
    rb_objc_define_method(rb_cArray, "<=>", rary_cmp, 1);
    rb_objc_define_method(rb_cArray, "assoc", rary_assoc, 1);
    rb_objc_define_method(rb_cArray, "rassoc", rary_rassoc, 1);
    rb_objc_define_method(rb_cArray, "flatten", rary_flatten, -1);
    rb_objc_define_method(rb_cArray, "flatten!", rary_flatten_bang, -1);
    rb_objc_define_method(rb_cArray, "product", rary_product, -1);
    rb_objc_define_method(rb_cArray, "combination", rary_combination, 1);
    rb_objc_define_method(rb_cArray, "permutation", rary_permutation, -1);
    rb_objc_define_method(rb_cArray, "cycle", rary_cycle, -1);
    rb_objc_define_method(rb_cArray, "sample", rary_sample, -1);
    rb_objc_define_method(rb_cArray, "rotate", rary_rotate, -1);
    rb_objc_define_method(rb_cArray, "rotate!", rary_rotate_bang, -1);
}

// MRI compatibility API.

VALUE
rb_ary_freeze(VALUE ary)
{
    return OBJ_FREEZE(ary);
}

VALUE
rb_ary_frozen_p(VALUE ary)
{
    return OBJ_FROZEN(ary) ? Qtrue : Qfalse;
}

long
rb_ary_len(VALUE ary)
{
    if (IS_RARY(ary)) {
	return RARY(ary)->len;
    }
    else {
	return [(id)ary count];
    }
}

VALUE
rb_ary_elt(VALUE ary, long offset)
{
    if (offset >= 0) {
	if (IS_RARY(ary)) {
	    if (offset < RARY(ary)->len) {
		return rary_elt(ary, offset);
	    }
	}
	else {
	    if (offset < [(id)ary count]) {
		return OC2RB([(id)ary objectAtIndex:offset]);
	    }
	}
    }
    return Qnil;
}

void
rb_ary_elt_set(VALUE ary, long offset, VALUE item)
{
    if (IS_RARY(ary)) {
	rary_elt_set(ary, offset, item);
    }
    else {
	TRY_MOP([(id)ary replaceObjectAtIndex:(NSUInteger)offset withObject:RB2OC(item)]);
    }
}

VALUE
rb_ary_replace(VALUE rcv, VALUE other)
{
    other = to_ary(other);
    rb_ary_clear(rcv);
    for (long i = 0, count = RARRAY_LEN(other); i < count; i++) {
	rb_ary_push(rcv, RARRAY_AT(other, i));
    }
    return rcv;
}

VALUE
rb_ary_clear(VALUE ary)
{
    if (IS_RARY(ary)) {
	return rary_clear(ary, 0);
    }
    else {
	return (VALUE)nsary_clear((id)ary, 0);
    }
}

static VALUE
recursive_join(VALUE ary, VALUE argp, int recur)
{
    VALUE *arg = (VALUE *)argp;
    if (recur) {
	rb_raise(rb_eArgError, "recursive array join");
    }
    return rb_ary_join(arg[0], arg[1]);
}

// TODO: Make it faster.
// rdoc spends a lot of time resizing strings concatenated by rb_ary_join.
// Resizing the string buffer using str_resize_bytes with something close
// to the final size before starting the concatenations would probably
// make it much faster.
VALUE
rb_ary_join(VALUE ary, VALUE sep)
{
    if (RARRAY_LEN(ary) == 0) {
	return rb_str_new(0, 0);
    }

    if (!NIL_P(sep)) {
	StringValue(sep);
    }

    bool taint = false;
    if (OBJ_TAINTED(ary) || OBJ_TAINTED(sep)) {
	taint = true;
    }
    bool untrust = false;
    if (OBJ_UNTRUSTED(ary) || OBJ_UNTRUSTED(sep)) {
        untrust = true;
    }

    VALUE result = rb_str_new(0, 0);

    for (long i = 0, count = RARRAY_LEN(ary); i < count; i++) {
	VALUE elem = RARRAY_AT(ary, i);
	switch (TYPE(elem)) {
	    case T_STRING:
		break;
	    case T_ARRAY:
		{
		    VALUE args[2] = {elem, sep};
		    elem = rb_exec_recursive(recursive_join, ary, (VALUE)args);
		}
		break;
	    default:
		{
		    VALUE tmp = rb_check_string_type(elem);
		    if (!NIL_P(tmp)) {
			elem = tmp;
			break;
		    }
		    tmp = rb_check_convert_type(elem, T_ARRAY, "Array", "to_ary");
		    if (!NIL_P(tmp)) {
			VALUE args[2] = {tmp, sep};
			elem = rb_exec_recursive(recursive_join, elem, (VALUE)args);
			break;
		    }
		    elem = rb_obj_as_string(elem);
		}
		break;
	}
	if (i > 0 && !NIL_P(sep)) {
	    rb_str_buf_append(result, sep);
	}
	rb_str_buf_append(result, elem);

	if (OBJ_TAINTED(elem)) {
	    taint = true;
	}
	if (OBJ_UNTRUSTED(elem)) {
	    untrust = true;
	}
    }

    if (taint) {
	OBJ_TAINT(result);
    }
    if (untrust) {
        OBJ_UNTRUST(result);
    }
    return result;
}

void
rb_ary_modify(VALUE ary)
{
    if (IS_RARY(ary)) {
	rary_modify(ary);
    }
    else {
	CHECK_MUTABLE((id)ary);
    }
}

VALUE
rb_ary_dup(VALUE ary)
{
    if (IS_RARY(ary)) {
	return rary_dup(ary, 0);
    }
    else {
	return (VALUE)nsary_dup((id)ary, 0);
    }
}

VALUE
rb_ary_reverse(VALUE ary)
{
    if (IS_RARY(ary)) {
	return rary_reverse_bang(ary, 0);
    }
    else {
	return (VALUE)nsary_reverse_bang((id)ary, 0);
    }
}

VALUE
rb_ary_includes(VALUE ary, VALUE item)
{
    if (IS_RARY(ary)) {
	return rary_includes(ary, 0, item);
    }
    else {
	return nsary_includes((id)ary, 0, item);
    }
}

VALUE
rb_ary_delete(VALUE ary, VALUE item)
{
    if (IS_RARY(ary)) {
	return rary_delete(ary, 0, item);
    }
    else {
	return nsary_delete((id)ary, 0, item);
    }
}

VALUE
rb_ary_delete_at(VALUE ary, long pos)
{
    if (IS_RARY(ary)) {
	return rary_delete_at(ary, 0, LONG2NUM(pos));
    }
    else {
	return nsary_delete_at((id)ary, 0, LONG2NUM(pos));
    }
}

VALUE
rb_ary_pop(VALUE ary)
{
    if (IS_RARY(ary)) {
	return rary_pop(ary, 0, 0, NULL);
    }
    else {
	return nsary_pop((id)ary, 0, 0, NULL);
    }
}

VALUE
rb_ary_shift(VALUE ary)
{
    if (IS_RARY(ary)) {
	return rary_shift(ary, 0, 0, NULL);
    }
    else {
	return nsary_shift((id)ary, 0, 0, NULL);
    }
}

VALUE
rb_ary_subseq(VALUE ary, long beg, long len)
{
   if (IS_RARY(ary)) {
	return rary_subseq(ary, beg, len);
   }
   else {
	return nsary_subseq((id)ary, beg, len);
   }
}

VALUE
rb_ary_entry(VALUE ary, long offset)
{
    if (IS_RARY(ary)) {
	return rary_entry(ary, offset);
    }
    else {
	return nsary_entry((id)ary, offset);
    }
}

VALUE
rb_ary_aref(int argc, VALUE *argv, VALUE ary)
{
    if (IS_RARY(ary)) {
	return rary_aref(ary, 0, argc, argv);
    }
    else {
	return nsary_aref((id)ary, 0, argc, argv);
    }
}

void
rb_ary_store(VALUE ary, long idx, VALUE val)
{
    if (IS_RARY(ary)) {
	rary_store(ary, idx, val);
    }
    else {
	nsary_store((id)ary, idx, val);
    }
}

void
rb_ary_insert(VALUE ary, long idx, VALUE val)
{
    if (IS_RARY(ary)) {
	rary_insert(ary, idx, val);
    }
    else {
	nsary_insert((id)ary, idx, val);
    }
}

VALUE
rb_ary_concat(VALUE x, VALUE y)
{
    if (IS_RARY(x)) {
	return rary_concat_m(x, 0, y);
    }
    else {
	return (VALUE)nsary_concat((id)x, 0, y);
    }
}

VALUE
rb_ary_push(VALUE ary, VALUE item) 
{
    if (IS_RARY(ary)) {
	return rary_push_m(ary, 0, item);
    }
    else {
	return (VALUE)nsary_push((id)ary, 0, item);
    }
}

VALUE
rb_ary_plus(VALUE x, VALUE y)
{
    if (IS_RARY(x)) {
	return rary_plus(x, 0, y);
    }
    else {
	return (VALUE)nsary_plus((id)x, 0, y);
    }
}

VALUE
rb_ary_unshift(VALUE ary, VALUE item)
{
    if (IS_RARY(ary)) {
	return rary_unshift(ary, 0, 1, &item);
    }
    else {
	return (VALUE)nsary_unshift((id)ary, 0, 1, &item);
    }
}

VALUE
rb_ary_each(VALUE ary)
{
    if (IS_RARY(ary)) {
	return rary_each(ary, 0);
    }
    else {
	return nsary_each((id)ary, 0);
    }
}

VALUE
rb_ary_sort(VALUE ary)
{
    if (IS_RARY(ary)) {
	return rary_sort(ary, 0);
    }
    else {
	return (VALUE)nsary_sort((id)ary, 0);
    }
}

VALUE
rb_ary_sort_bang(VALUE ary)
{
    if (IS_RARY(ary)) {
	return rary_sort_bang(ary, 0);
    }
    else {
	return (VALUE)nsary_sort_bang((id)ary, 0);
    }
}

static void *nsary_cptr_key = NULL;

const VALUE *
rb_ary_ptr(VALUE ary)
{
    if (IS_RARY(ary)) {
	return rary_ptr(ary);
    }

    id nsary = (id)ary;
    const long len = [nsary count];
    if (len == 0) {
	return NULL;
    }

    VALUE *values = (VALUE *)xmalloc(sizeof(VALUE) * len);
    for (long i = 0; i < len; i++) {
	values[i] = OC2RB([nsary objectAtIndex:i]);	
    }

    rb_objc_set_associative_ref((void *)nsary, &nsary_cptr_key, values);

    return values;
}

static unsigned long
recursive_hash(VALUE ary, VALUE dummy, int recur)
{
    long i;
    st_index_t h;
    VALUE n;

    h = rb_hash_start(RARRAY_LEN(ary));
    if (recur) {
	h = rb_hash_uint(h, NUM2LONG(rb_hash(rb_cArray)));
    }
    else {
	for (i=0; i<RARRAY_LEN(ary); i++) {
	    n = rb_hash(RARRAY_PTR(ary)[i]);
	    h = rb_hash_uint(h, NUM2LONG(n));
	}
    }
    h = rb_hash_end(h);
    return h;
}

unsigned long
rb_ary_hash(VALUE ary)
{
    return rb_exec_recursive_outer(recursive_hash, ary, 0);
}
