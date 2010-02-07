/*
 * MacRuby extensions to NSDictionary.
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
#include "hash.h"

VALUE rb_cHash;
VALUE rb_cNSHash;
VALUE rb_cNSMutableHash;
static VALUE rb_cCFHash;

static id
to_hash(id hash)
{
    return (id)rb_convert_type((VALUE)hash, T_HASH, "Hash", "to_hash");
}

static id
nshash_dup(id rcv, SEL sel)
{
    id dup = [rcv mutableCopy];
    if (OBJ_TAINTED(rcv)) {
	OBJ_TAINT(dup);
    }
    return dup;
}

static id
nshash_clone(id rcv, SEL sel)
{
    id clone = nshash_dup(rcv, 0);
    if (OBJ_FROZEN(rcv)) {
	OBJ_FREEZE(clone);
    }
    return clone;
}

static id
nshash_rehash(id rcv, SEL sel)
{
    NSArray *keys = [rcv allKeys];
    NSArray *values = [rcv allValues];
    assert([keys count] == [values count]);
    [rcv removeAllObjects];
    for (unsigned i = 0, count = [keys count]; i < count; i++) {
	[rcv setObject:[values objectAtIndex:i] forKey:[keys objectAtIndex:i]];
    }
    return rcv;
}

static id
nshash_to_hash(id rcv, SEL sel)
{
    return rcv;
}

static id
nshash_to_a(id rcv, SEL sel)
{
    NSMutableArray *ary = [NSMutableArray new];
    for (id key in rcv) {
	id value = [rcv objectForKey:key];
	[ary addObject:[NSArray arrayWithObjects:key, value, nil]];
    }
    return ary;
}

static id
nshash_inspect(id rcv, SEL sel)
{
    NSMutableString *str = [NSMutableString new];
    [str appendString:@"{"];
    for (id key in rcv) {
	if ([str length] > 1) {
	    [str appendString:@", "];
	}
	id value = [rcv objectForKey:key];
	[str appendString:(NSString *)rb_inspect(OC2RB(key))];
	[str appendString:@"=>"];
	[str appendString:(NSString *)rb_inspect(OC2RB(value))];
    }
    [str appendString:@"}"];
    return str;
}

static VALUE
nshash_equal(id rcv, SEL sel, id other)
{
    return [rcv isEqualToDictionary:other] ? Qtrue : Qfalse;
}

static inline VALUE
nshash_lookup(id rcv, VALUE key)
{
    return OC2RB([rcv objectForKey:RB2OC(key)]);
}

static VALUE
nshash_aref(id rcv, SEL sel, VALUE key)
{
    return nshash_lookup(rcv, key);
}

static VALUE
nshash_aset(id rcv, SEL sel, VALUE key, VALUE val)
{
    [rcv setObject:RB2OC(val) forKey:RB2OC(key)];
    return val;
}

static VALUE
nshash_fetch(id rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE key, if_none;
    rb_scan_args(argc, argv, "11", &key, &if_none);

    const bool block_given = rb_block_given_p();
    if (block_given && argc == 2) {
	rb_warn("block supersedes default value argument");
    }

    id value = [rcv objectForKey:RB2OC(key)];
    if (value != nil) {
	return OC2RB(value);
    }
    if (block_given) {
	return rb_yield(key);
    }
    if (argc == 1) {
	rb_raise(rb_eKeyError, "key not found");
    }
    return if_none;
}

static VALUE
nshash_default(id rcv, SEL sel, int argc, VALUE *argv)
{
    // TODO
    return Qnil;
}

static VALUE
nshash_set_default(id rcv, SEL sel, VALUE default_value)
{
    // TODO
    return Qnil;
}

static VALUE
nshash_default_proc(id rcv, SEL sel)
{
    // Default procs are never possible with NSDictionaries.
    return Qnil;
}

static VALUE
nshash_key(id rcv, SEL sel, VALUE value)
{
    NSArray *keys = [rcv allKeysForObject:RB2OC(value)];
    if ([keys count] > 0) {
	return OC2RB([keys objectAtIndex:0]);
    }
    return Qnil;
}

static VALUE
nshash_index(id rcv, SEL sel, VALUE value)
{
    rb_warn("Hash#index is deprecated; use Hash#key");
    return nshash_key(rcv, 0, value);
}

static VALUE
nshash_size(id rcv, SEL sel)
{
    return LONG2FIX([rcv count]);
}

static VALUE
nshash_empty(id rcv, SEL sel)
{
    return [rcv count] == 0 ? Qtrue : Qfalse;
}

static VALUE
nshash_each_value(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    for (id key in rcv) {
	rb_yield(OC2RB([rcv objectForKey:key]));
	RETURN_IF_BROKEN();
    }
    return (VALUE)rcv;
}

static VALUE
nshash_each_key(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    for (id key in rcv) {
	rb_yield(OC2RB(key));
	RETURN_IF_BROKEN();
    }
    return (VALUE)rcv;
}

static VALUE
nshash_each_pair(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    for (id key in rcv) {
	id value = [rcv objectForKey:key];
	rb_yield(rb_assoc_new(OC2RB(key), OC2RB(value)));
	RETURN_IF_BROKEN();
    }
    return (VALUE)rcv;
}

static id
nshash_keys(id rcv, SEL sel)
{
    return [[rcv allKeys] mutableCopy];
}

static id
nshash_values(id rcv, SEL sel)
{
    return [[rcv allValues] mutableCopy];
}

static id
nshash_values_at(id rcv, SEL sel, int argc, VALUE *argv)
{
    NSMutableArray *ary = [NSMutableArray new];
    for (int i = 0; i < argc; i++) {
	id value = [rcv objectForKey:RB2OC(argv[i])];
	[ary addObject:value];
    }
    return ary;
}

static VALUE
nshash_shift(id rcv, SEL sel)
{
    if ([rcv count] > 0) {
	id key = [[rcv keyEnumerator] nextObject];
	assert(key != NULL);
	id value = [rcv objectForKey:key];
	[rcv removeObjectForKey:key];
	return rb_assoc_new(OC2RB(key), OC2RB(value));
    }
    return nshash_default(rcv, 0, 0, NULL);
}

static VALUE
nshash_delete(id rcv, SEL sel, VALUE key)
{
    id ockey = RB2OC(key);
    id value = [rcv objectForKey:ockey];
    if (value != nil) {
	[rcv removeObjectForKey:ockey];
	return OC2RB(value);
    }
    if (rb_block_given_p()) {
	return rb_yield(key);
    }
    return Qnil;
}

static VALUE
nshash_delete_if(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    NSMutableArray *ary = [NSMutableArray new];
    for (id key in rcv) {
	id value = [rcv objectForKey:key];
	if (RTEST(rb_yield_values(2, OC2RB(key), OC2RB(value)))) {
	    [ary addObject:key];
	}
	RETURN_IF_BROKEN();
    }
    [rcv removeObjectsForKeys:ary];
    return (VALUE)rcv;
}

static VALUE
nshash_select(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    NSMutableDictionary *dict = [NSMutableDictionary new];
    for (id key in rcv) {
	id value = [rcv objectForKey:key];
	if (RTEST(rb_yield_values(2, OC2RB(key), OC2RB(value)))) {
	    [dict setObject:value forKey:key];
	}
	RETURN_IF_BROKEN();
    }
    return (VALUE)dict;
}

static VALUE
nshash_reject(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    return nshash_delete_if([rcv mutableCopy], 0); 
}

static VALUE
nshash_reject_bang(id rcv, SEL sel)
{
    RETURN_ENUMERATOR(rcv, 0, 0);
    unsigned size = [rcv count];
    nshash_delete_if(rcv, 0);
    return size != [rcv count] ? (VALUE)rcv : Qnil;
}

static id
nshash_clear(id rcv, SEL sel)
{
    [rcv removeAllObjects];
    return rcv;
}

static id
nshash_update(id rcv, SEL sel, id hash)
{
    hash = to_hash(hash);
    if (rb_block_given_p()) {
	for (id key in hash) {
	    id value = [hash objectForKey:key];
	    id old_value = [rcv objectForKey:key];
	    if (old_value != nil) {
		value = RB2OC(rb_yield_values(3, OC2RB(key), OC2RB(old_value),
			    OC2RB(value)));
	    }
	    [rcv setObject:value forKey:key];
	}
    }
    else {
	for (id key in hash) {
	    id value = [hash objectForKey:key];
	    [rcv setObject:value forKey:key];
	}
    }    
    return rcv;
}

static id
nshash_merge(id rcv, SEL sel, id hash)
{
    return nshash_update([rcv mutableCopy], 0, hash);
}

static id
nshash_replace(id rcv, SEL sel, id hash)
{
    hash = to_hash(hash);
    [rcv setDictionary:hash];
    return rcv;
}

static VALUE
nshash_assoc(id rcv, SEL sel, VALUE obj)
{
    for (id key in rcv) {
	if (rb_equal(OC2RB(key), obj)) {
	    id value = [rcv objectForKey:key];
	    return rb_assoc_new(obj, OC2RB(value));
	}
    }
    return Qnil;
}

static VALUE
nshash_rassoc(id rcv, SEL sel, VALUE obj)
{
    for (id key in rcv) {
	id value = [rcv objectForKey:key];
	if (rb_equal(OC2RB(value), obj)) {
	    return rb_assoc_new(OC2RB(key), obj);
	}
    }
    return Qnil;
}

static id
nshash_flatten(id rcv, SEL sel, int argc, VALUE *argv)
{
    id ary = nshash_to_a(rcv, 0);
    VALUE tmp;
    if (argc == 0) {
	argc = 1;
	tmp = INT2FIX(1);
	argv = &tmp;
    }
    rb_vm_call((VALUE)ary, sel_registerName("flatten!:"), argc, argv, false);
    return ary;
}

static VALUE
nshash_has_key(id rcv, SEL sel, VALUE key)
{
    return [rcv objectForKey:RB2OC(key)] == nil ? Qfalse : Qtrue;
}

static VALUE
nshash_has_value(id rcv, SEL sel, VALUE value)
{
    return [[rcv allKeysForObject:RB2OC(value)] count] == 0 ? Qfalse : Qtrue;
}

static id
nshash_compare_by_id(id rcv, SEL sel)
{
    // Not implemented.
    return rcv;
}

static VALUE
nshash_compare_by_id_p(id rcv, SEL sel)
{
    // Not implemented.
    return Qfalse;
}

void
Init_NSDictionary(void)
{
#if MAC_OS_X_VERSION_MAX_ALLOWED < 1070
    rb_cCFHash = (VALUE)objc_getClass("NSCFDictionary");
#else
    rb_cCFHash = (VALUE)objc_getClass("__NSCFDictionary");
#endif
    assert(rb_cCFHash != 0);
    rb_cNSHash = (VALUE)objc_getClass("NSDictionary");
    assert(rb_cNSHash != 0);
    rb_cHash = rb_cNSHash;
    rb_cNSMutableHash = (VALUE)objc_getClass("NSMutableDictionary");
    assert(rb_cNSMutableHash != 0);

    rb_include_module(rb_cHash, rb_mEnumerable);

    rb_objc_define_method(rb_cHash, "dup", nshash_dup, 0);
    rb_objc_define_method(rb_cHash, "clone", nshash_clone, 0);
    rb_objc_define_method(rb_cHash, "rehash", nshash_rehash, 0);
    rb_objc_define_method(rb_cHash, "to_hash", nshash_to_hash, 0);
    rb_objc_define_method(rb_cHash, "to_a", nshash_to_a, 0);
    rb_objc_define_method(rb_cHash, "to_s", nshash_inspect, 0);
    rb_objc_define_method(rb_cHash, "inspect", nshash_inspect, 0);
    rb_objc_define_method(rb_cHash, "==", nshash_equal, 1);
    rb_objc_define_method(rb_cHash, "eql?", nshash_equal, 1);
    rb_objc_define_method(rb_cHash, "[]", nshash_aref, 1);
    rb_objc_define_method(rb_cHash, "[]=", nshash_aset, 2);
    rb_objc_define_method(rb_cHash, "fetch", nshash_fetch, -1);
    rb_objc_define_method(rb_cHash, "store", nshash_aset, 2);
    rb_objc_define_method(rb_cHash, "default", nshash_default, -1);
    rb_objc_define_method(rb_cHash, "default=", nshash_set_default, 1);
    rb_objc_define_method(rb_cHash, "default_proc", nshash_default_proc, 0);
    rb_objc_define_method(rb_cHash, "key", nshash_key, 1);
    rb_objc_define_method(rb_cHash, "index", nshash_index, 1);
    rb_objc_define_method(rb_cHash, "size", nshash_size, 0);
    rb_objc_define_method(rb_cHash, "length", nshash_size, 0);
    rb_objc_define_method(rb_cHash, "empty?", nshash_empty, 0);
    rb_objc_define_method(rb_cHash, "each_value", nshash_each_value, 0);
    rb_objc_define_method(rb_cHash, "each_key", nshash_each_key, 0);
    rb_objc_define_method(rb_cHash, "each_pair", nshash_each_pair, 0);
    rb_objc_define_method(rb_cHash, "each", nshash_each_pair, 0);
    rb_objc_define_method(rb_cHash, "keys", nshash_keys, 0);
    rb_objc_define_method(rb_cHash, "values", nshash_values, 0);
    rb_objc_define_method(rb_cHash, "values_at", nshash_values_at, -1);
    rb_objc_define_method(rb_cHash, "shift", nshash_shift, 0);
    rb_objc_define_method(rb_cHash, "delete", nshash_delete, 1);
    rb_objc_define_method(rb_cHash, "delete_if", nshash_delete_if, 0);
    rb_objc_define_method(rb_cHash, "select", nshash_select, 0);
    rb_objc_define_method(rb_cHash, "reject", nshash_reject, 0);
    rb_objc_define_method(rb_cHash, "reject!", nshash_reject_bang, 0);
    rb_objc_define_method(rb_cHash, "clear", nshash_clear, 0);
    // XXX: #invert is a private method on NSMutableDictionary, so to not
    // break things we do not implement it.
    rb_objc_define_method(rb_cHash, "update", nshash_update, 1);
    rb_objc_define_method(rb_cHash, "merge!", nshash_update, 1);
    rb_objc_define_method(rb_cHash, "merge", nshash_merge, 1);
    rb_objc_define_method(rb_cHash, "replace", nshash_replace, 1);
    rb_objc_define_method(rb_cHash, "assoc", nshash_assoc, 1);
    rb_objc_define_method(rb_cHash, "rassoc", nshash_rassoc, 1);
    rb_objc_define_method(rb_cHash, "flatten", nshash_flatten, -1);
    rb_objc_define_method(rb_cHash, "include?", nshash_has_key, 1);
    rb_objc_define_method(rb_cHash, "member?", nshash_has_key, 1);
    rb_objc_define_method(rb_cHash, "key?", nshash_has_key, 1);
    rb_objc_define_method(rb_cHash, "has_key?", nshash_has_key, 1);
    rb_objc_define_method(rb_cHash, "value?", nshash_has_value, 1);
    rb_objc_define_method(rb_cHash, "has_value?", nshash_has_value, 1);
    rb_objc_define_method(rb_cHash, "compare_by_identity",
	    nshash_compare_by_id, 0);
    rb_objc_define_method(rb_cHash, "compare_by_identity?",
	    nshash_compare_by_id_p, 0);
}

// NSDictionary + NSMutableDictionary primitives. These are added automatically
// on singleton classes of pure NSDictionaries. Our implementation just calls
// the original methods, by tricking the receiver's class.

#define PREPARE_RCV(x) \
    Class __old = *(Class *)x; \
    *(Class *)x = (Class)rb_cCFHash;

#define RESTORE_RCV(x) \
    *(Class *)x = __old;

static unsigned
nshash_count(id rcv, SEL sel) 
{
    PREPARE_RCV(rcv);
    const unsigned count = [rcv count];
    RESTORE_RCV(rcv);
    return count; 
}

static id
nshash_keyEnumerator(id rcv, SEL sel)
{
    PREPARE_RCV(rcv);
    id keys = [rcv allKeys]; 
    RESTORE_RCV(rcv);
    return [keys objectEnumerator];
}

static id
nshash_objectForKey(id rcv, SEL sel, id key)
{
    PREPARE_RCV(rcv);
    id value = [rcv objectForKey:key];
    RESTORE_RCV(rcv);
    return value;
}

static void 
nshash_setObjectForKey(id rcv, SEL sel, id value, id key) 
{
    PREPARE_RCV(rcv);
    [rcv setObject:value forKey:key];
    RESTORE_RCV(rcv);
} 

static void
nshash_getObjectsAndKeys(id rcv, SEL sel, id *objs, id *keys)
{
    PREPARE_RCV(rcv);
    [rcv getObjects:objs andKeys:keys];
    RESTORE_RCV(rcv);
}

static void
nshash_removeObjectForKey(id rcv, SEL sel, id key)
{
    PREPARE_RCV(rcv);
    [rcv removeObjectForKey:key];
    RESTORE_RCV(rcv);
}

static void
nshash_removeAllObjects(id rcv, SEL sel)
{
    PREPARE_RCV(rcv);
    [rcv removeAllObjects];
    RESTORE_RCV(rcv);
}

static bool
nshash_isEqual(id rcv, SEL sel, id other)
{
    PREPARE_RCV(rcv);
    const bool res = [rcv isEqualToDictionary:other];
    RESTORE_RCV(rcv);
    return res;
}

static bool
nshash_containsObject(id rcv, SEL sel, id value)
{
    PREPARE_RCV(rcv);
    const bool res = [[rcv allKeysForObject:value] count] > 0;
    RESTORE_RCV(rcv);
    return res;
}

void
rb_objc_install_hash_primitives(Class klass)
{
    rb_objc_install_method2(klass, "count", (IMP)nshash_count);
    rb_objc_install_method2(klass, "keyEnumerator", (IMP)nshash_keyEnumerator);
    rb_objc_install_method2(klass, "objectForKey:", (IMP)nshash_objectForKey);
    rb_objc_install_method2(klass, "getObjects:andKeys:",
	    (IMP)nshash_getObjectsAndKeys);
    rb_objc_install_method2(klass, "isEqual:", (IMP)nshash_isEqual);
    rb_objc_install_method2(klass, "containsObject:",
	    (IMP)nshash_containsObject);

    const bool mutable =
	class_getSuperclass(klass) == (Class)rb_cNSMutableHash; 

    if (mutable) {
	rb_objc_install_method2(klass, "setObject:forKey:",
		(IMP)nshash_setObjectForKey);
	rb_objc_install_method2(klass, "removeObjectForKey:",
		(IMP)nshash_removeObjectForKey);
	rb_objc_install_method2(klass, "removeAllObjects",
		(IMP)nshash_removeAllObjects);
    }

    //rb_objc_define_method(*(VALUE *)klass, "alloc", hash_alloc, 0);
}

// MRI compatibility API.

VALUE
rb_hash_dup(VALUE rcv)
{
    if (IS_RHASH(rcv)) {
	return rhash_dup(rcv, 0);
    }
    else {
	return (VALUE)nshash_dup((id)rcv, 0);
    }
}

void
rb_hash_foreach(VALUE hash, int (*func)(ANYARGS), VALUE farg)
{
    if (IS_RHASH(hash)) {
	rhash_foreach(hash, func, farg);
    }
    else {
	for (id key in (id)hash) {
	    id value = [(id)hash objectForKey:key];
	    if ((*func)(OC2RB(key), OC2RB(value), farg) == ST_STOP) {
		break;
	    }
	}
    }
}

VALUE
rb_hash_lookup(VALUE hash, VALUE key)
{
    if (IS_RHASH(hash)) {
	VALUE val = rhash_lookup(hash, key);
	return val == Qundef ? Qnil : val;
    }
    else {
	return nshash_lookup((id)hash, key);
    }
}

VALUE
rb_hash_aref(VALUE hash, VALUE key)
{
    if (IS_RHASH(hash)) {
	return rhash_aref(hash, 0, key);
    }
    else {
	return nshash_lookup((id)hash, key);
    }
}

VALUE
rb_hash_delete_key(VALUE hash, VALUE key)
{
    if (IS_RHASH(hash)) {
	rhash_modify(hash);
	return rhash_delete_key(hash, key);
    }
    else {
	id ockey = RB2OC(key);
	id value = [(id)hash objectForKey:ockey];
	if (value != nil) {
	    [(id)hash removeObjectForKey:ockey];
	    return OC2RB(value);
	}
	return Qundef;
    }
}

VALUE
rb_hash_delete(VALUE hash, VALUE key)
{
    VALUE val = rb_hash_delete_key(hash, key);
    if (val != Qundef) {
	return val;
    }
    return Qnil;
}

VALUE
rb_hash_aset(VALUE hash, VALUE key, VALUE val)
{
    if (IS_RHASH(hash)) {
	return rhash_aset(hash, 0, key, val);
    }
    else {
	return nshash_aset((id)hash, 0, key, val);
    }
}

long
rb_hash_size(VALUE hash)
{
    if (IS_RHASH(hash)) {
	return rhash_len(hash);
    }
    else {
	return [(id)hash count];
    }
}

VALUE
rb_hash_keys(VALUE hash)
{
    if (IS_RHASH(hash)) {
	return rhash_keys(hash, 0);
    }
    else {
	return (VALUE)nshash_keys((id)hash, 0);
    }
}

VALUE
rb_hash_has_key(VALUE hash, VALUE key)
{
    if (IS_RHASH(hash)) {
	return rhash_has_key(hash, 0, key);
    }
    else {
	return nshash_has_key((id)hash, 0, key);
    }
}

VALUE
rb_hash_set_ifnone(VALUE hash, VALUE ifnone)
{
    if (IS_RHASH(hash)) {
	return rhash_set_default(hash, 0, ifnone);
    }
    else {
	return nshash_set_default((id)hash, 0, ifnone);
    }
}
