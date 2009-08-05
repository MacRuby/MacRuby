/*
 * MacRuby CFSet-based--implementation of Ruby 1.9's lib/set.rb
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2007-2008, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include "ruby/ruby.h"
#include "id.h"
#include "objc.h"

VALUE rb_cSet;
VALUE rb_cNSSet, rb_cNSMutableSet, rb_cCFSet;

static inline void
rb_set_modify_check(VALUE set)
{
    long mask;
    mask = rb_objc_flag_get_mask((void *)set);
    if (RSET_IMMUTABLE(set)) {
	mask |= FL_FREEZE;
    }
    if ((mask & FL_FREEZE) == FL_FREEZE) {
	rb_raise(rb_eRuntimeError, "can't modify frozen/immutable set");
    }
    if ((mask & FL_TAINT) == FL_TAINT && rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "Insecure: can't modify set");
    }
}

static VALUE
set_alloc(VALUE klass)
{
    CFMutableSetRef set;

    set = CFSetCreateMutable(NULL, 0, &kCFTypeSetCallBacks);
    if (klass != 0 && klass != rb_cNSSet && klass != rb_cNSMutableSet)
	*(Class *)set = (Class)klass;

    CFMakeCollectable(set);

    return (VALUE)set;
}

VALUE
rb_set_dup(VALUE rcv, SEL sel)
{
    VALUE dup = (VALUE)CFSetCreateMutableCopy(NULL, 0, (CFSetRef)rcv);
    if (OBJ_TAINTED(rcv)) {
	OBJ_TAINT(dup);
    }
    CFMakeCollectable((CFTypeRef)dup);
    return dup;
}

static VALUE
rb_set_clone(VALUE rcv, SEL sel)
{
    VALUE clone = rb_set_dup(rcv, 0);
    if (OBJ_FROZEN(rcv)) {
	OBJ_FREEZE(clone);
    }
    return clone;
}

VALUE
rb_set_new(void)
{
    return set_alloc(0);
}

static VALUE
rb_set_s_create(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    int i;

    VALUE set = set_alloc(klass);

    for (i = 0; i < argc; i++) {
	CFSetAddValue((CFMutableSetRef)set, RB2OC(argv[i]));
    }

    return set;
}

static VALUE
rb_set_size(VALUE set, SEL sel)
{
    return INT2FIX(CFSetGetCount((CFSetRef)set));
}

static VALUE
rb_set_empty_q(VALUE set, SEL sel)
{
    return CFSetGetCount((CFSetRef)set) == 0 ? Qtrue : Qnil;
}

static void
rb_set_intersect_callback(const void *value, void *context)
{
    CFMutableSetRef *sets = (CFMutableSetRef *)context;
    if (CFSetContainsValue(sets[0], RB2OC(value)))
	CFSetAddValue(sets[1], RB2OC(value));
}

static VALUE
rb_set_intersect(VALUE set, SEL sel, VALUE other)
{
    rb_set_modify_check(set);

    VALUE new_set = rb_set_new();
    CFMutableSetRef sets[2] = { (CFMutableSetRef)other, (CFMutableSetRef)new_set };
    CFSetApplyFunction((CFMutableSetRef)set, rb_set_intersect_callback, sets);

    return new_set;
}

static void
rb_set_union_callback(const void *value, void *context)
{
    CFMutableSetRef set = context;
    if (!CFSetContainsValue(set, RB2OC(value)))
	CFSetAddValue(set, RB2OC(value));
}

static VALUE
merge_i(VALUE val, VALUE *args)
{
    VALUE set = (VALUE)args;
    if (!CFSetContainsValue((CFMutableSetRef)set, (const void *)RB2OC(val))) {
	CFSetAddValue((CFMutableSetRef)set, (const void *)RB2OC(val));
    }

    return Qnil;
}

static VALUE
rb_set_merge(VALUE set, SEL sel, VALUE other)
{
    rb_set_modify_check(set);

    VALUE klass = *(VALUE *)other;
    if (klass == rb_cCFSet || klass == rb_cNSSet || klass == rb_cNSMutableSet)
	CFSetApplyFunction((CFMutableSetRef)other, rb_set_union_callback, (void *)set);	
    else
	rb_block_call(other, rb_intern("each"), 0, 0, merge_i, (VALUE)set);

    return set;
}

static VALUE
rb_set_union(VALUE set, SEL sel, VALUE other)
{
    VALUE new_set = rb_set_dup(set, 0);
    rb_set_merge(new_set, 0, other);

    return new_set;
}

static void
rb_set_subtract_callback(const void *value, void *context)
{
    CFMutableSetRef *sets = context;
    if (CFSetContainsValue(sets[0], RB2OC(value)))
	CFSetRemoveValue(sets[1], RB2OC(value));
}

static VALUE
rb_set_subtract(VALUE set, SEL sel, VALUE other)
{
    VALUE new_set = rb_set_dup(set, 0);
    CFMutableSetRef sets[2] = { (CFMutableSetRef)other, (CFMutableSetRef)new_set };
    CFSetApplyFunction((CFMutableSetRef)set, rb_set_subtract_callback, sets);

    return new_set;
}

static VALUE
rb_set_add(VALUE set, SEL sel, VALUE obj)
{
    rb_set_modify_check(set);

    CFSetAddValue((CFMutableSetRef)set, (const void *)RB2OC(obj));

    return set;
}

static VALUE
rb_set_add2(VALUE set, SEL sel, VALUE obj)
{
    rb_set_modify_check(set);

    if (CFSetContainsValue((CFMutableSetRef)set, (const void *)RB2OC(obj))) {
	return Qnil;
    }
    CFSetAddValue((CFMutableSetRef)set, (const void *)RB2OC(obj));
    return set;
}

static VALUE
rb_set_clear(VALUE set, SEL sel)
{
    rb_set_modify_check(set);

    CFSetRemoveAllValues((CFMutableSetRef)set);
    return set;
}

static VALUE
rb_set_delete(VALUE set, SEL sel, VALUE obj)
{
    rb_set_modify_check(set);

    CFSetRemoveValue((CFMutableSetRef)set, (const void *)RB2OC(obj));
    return set;
}

static VALUE
rb_set_delete2(VALUE set, SEL sel, VALUE obj)
{
    rb_set_modify_check(set);

    if (CFSetContainsValue((CFMutableSetRef)set, (const void *)RB2OC(obj))) {
	CFSetRemoveValue((CFMutableSetRef)set, (const void *)RB2OC(obj));
	return set;
    } else
	return Qnil;
}

static void
rb_set_delete_if_callback(const void *value, void *context)
{
    VALUE set = ((VALUE *)context)[0];
    VALUE *acted = ((VALUE **)context)[1];
    if (rb_yield(OC2RB(value)) == Qtrue) {
	*acted = Qtrue;
	rb_set_delete(set, 0, (VALUE)value);
    }
}

static VALUE
rb_set_delete_if(VALUE set, SEL sel)
{
    rb_set_modify_check(set);

    VALUE new_set = rb_set_dup(set, 0);
    VALUE acted = Qfalse;
    VALUE context[2] = { set, (VALUE)&acted };
    CFSetApplyFunction((CFMutableSetRef)new_set, rb_set_delete_if_callback, (void *)context);

    return set;
}

static VALUE
rb_set_reject_bang(VALUE set, SEL sel)
{
    rb_set_modify_check(set);

    VALUE new_set = rb_set_dup(set, 0);
    VALUE acted = Qfalse;
    VALUE context[2] = { set, (VALUE)&acted };
    CFSetApplyFunction((CFMutableSetRef)new_set, rb_set_delete_if_callback, (void *)context);

    return acted == Qtrue ? set : Qnil ;
}

static void
rb_set_each_callback(const void *value, void *context)
{
    rb_yield((VALUE)OC2RB(value));
}

static VALUE
rb_set_each(VALUE set, SEL sel)
{
    RETURN_ENUMERATOR(set, 0, 0);
    CFSetApplyFunction((CFMutableSetRef)set, rb_set_each_callback, NULL);
    return Qnil;
}

static VALUE
rb_set_include(VALUE set, SEL sel, VALUE obj)
{
    return CFSetContainsValue((CFMutableSetRef)set, (const void *)RB2OC(obj)) ? Qtrue : Qfalse;
}

static void
rb_set_to_a_callback(const void *value, void *context)
{
    rb_ary_push((VALUE)context, (VALUE)value);
}

static VALUE
rb_set_to_a(VALUE set, SEL sel)
{
    VALUE ary = rb_ary_new();
    CFSetApplyFunction((CFMutableSetRef)set, rb_set_to_a_callback, (void *)ary);

    return ary;
}

static VALUE
rb_set_equal(VALUE set, SEL sel, VALUE other)
{
    return CFEqual((CFTypeRef)set, (CFTypeRef)RB2OC(other)) ? Qtrue : Qfalse;
}

static VALUE
initialize_i(VALUE val, VALUE *args)
{
    if (rb_block_given_p()) {
	val = rb_yield(val);
    }
    rb_set_add((VALUE)args, 0, val);

    return Qnil;
}

static VALUE
rb_set_initialize(VALUE set, SEL sel, int argc, VALUE *argv)
{
    VALUE val;

    set = (VALUE)objc_msgSend((id)set, selInit);

    rb_scan_args(argc, argv, "01", &val);
    if (!NIL_P(val)) {
	rb_objc_block_call(val, selEach, cacheEach, 0, 0, initialize_i, (VALUE)set);
    } 
    else if (rb_block_given_p()) {
	rb_warning("given block not used");
    }

    return set;
}

#define PREPARE_RCV(x) \
    Class old = *(Class *)x; \
    *(Class *)x = (Class)rb_cCFSet;

#define RESTORE_RCV(x) \
    *(Class *)x = old;

bool rb_objc_set_is_pure(VALUE set)
{
    return *(Class *)set == (Class)rb_cCFSet;
}

static CFIndex
imp_rb_set_count(void *rcv, SEL sel)
{
    CFIndex count;
    PREPARE_RCV(rcv);
    count = CFSetGetCount((CFSetRef)rcv);
    RESTORE_RCV(rcv);
    return count;
}

static const void *
imp_rb_set_member(void *rcv, SEL sel, void *obj)
{
    void *ret;
    PREPARE_RCV(rcv);
    ret = CFSetContainsValue((CFSetRef)rcv, obj) ? obj : NULL;
    RESTORE_RCV(rcv);
    return ret;
}

static const void *
imp_rb_set_objectEnumerator(void *rcv, SEL sel)
{
    void *ret;
    PREPARE_RCV(rcv);
    ret = objc_msgSend(rcv, sel);
    RESTORE_RCV(rcv);
    return ret;
}

static void
imp_rb_set_addOrRemoveObject(void *rcv, SEL sel, void *obj)
{
    PREPARE_RCV(rcv);
    objc_msgSend(rcv, sel, obj);
    RESTORE_RCV(rcv);
}

static unsigned long
imp_rb_set_countByEnumeratingWithStateObjectsCount(void *rcv, SEL sel, void *state, void *objects, unsigned long count)
{
    unsigned long ret;
    PREPARE_RCV(rcv);
    ret = (unsigned long)objc_msgSend(rcv, sel, state, objects, count);
    RESTORE_RCV(rcv);
    return ret;
}

void
rb_objc_install_set_primitives(Class klass)
{
    rb_objc_install_method2(klass, "count", (IMP)imp_rb_set_count);
    rb_objc_install_method2(klass, "member:", (IMP)imp_rb_set_member);
    rb_objc_install_method2(klass, "objectEnumerator", (IMP)imp_rb_set_objectEnumerator);
    rb_objc_install_method2(klass, "addObject:", (IMP)imp_rb_set_addOrRemoveObject);
    rb_objc_install_method2(klass, "removeObject:", (IMP)imp_rb_set_addOrRemoveObject);
    rb_objc_install_method2(klass, "countByEnumeratingWithState:objects:count:", (IMP)imp_rb_set_countByEnumeratingWithStateObjectsCount);

    rb_define_alloc_func((VALUE)klass, set_alloc);
}

void
Init_Set(void)
{
    rb_cCFSet = (VALUE)objc_getClass("NSCFSet");
    rb_cSet = rb_cNSSet = (VALUE)objc_getClass("NSSet");
    rb_cNSMutableSet = (VALUE)objc_getClass("NSMutableSet");
    rb_set_class_path(rb_cNSMutableSet, rb_cObject, "NSMutableSet");
    rb_const_set(rb_cObject, rb_intern("Set"), rb_cNSMutableSet);

    rb_include_module(rb_cSet, rb_mEnumerable);

    rb_objc_define_method(*(VALUE *)rb_cSet, "[]", rb_set_s_create, -1);

    rb_objc_define_method(rb_cSet, "dup", rb_set_dup, 0);
    rb_objc_define_method(rb_cSet, "clone", rb_set_clone, 0);
    rb_objc_define_method(rb_cSet, "initialize", rb_set_initialize, -1);

    rb_objc_define_method(rb_cSet, "to_a", rb_set_to_a, 0);
    rb_objc_define_method(rb_cSet, "==", rb_set_equal, 1);
    rb_objc_define_method(rb_cSet, "size", rb_set_size, 0);
    rb_objc_define_method(rb_cSet, "empty?", rb_set_empty_q, 0);
    rb_define_alias(rb_cSet, "length", "size");
    rb_objc_define_method(rb_cSet, "&", rb_set_intersect, 1);
    rb_define_alias(rb_cSet, "intersect", "&");
    rb_objc_define_method(rb_cSet, "|", rb_set_union, 1);
    rb_define_alias(rb_cSet, "union", "|");
    rb_define_alias(rb_cSet, "+", "|");
    rb_objc_define_method(rb_cSet, "merge", rb_set_merge, 1);
    rb_objc_define_method(rb_cSet, "-", rb_set_subtract, 1);
    rb_objc_define_method(rb_cSet, "add", rb_set_add, 1);
    rb_define_alias(rb_cSet, "<<", "add");
    rb_objc_define_method(rb_cSet, "add?", rb_set_add2, 1);
    rb_objc_define_method(rb_cSet, "clear", rb_set_clear, 0);
    rb_objc_define_method(rb_cSet, "delete", rb_set_delete, 1);
    rb_objc_define_method(rb_cSet, "delete?", rb_set_delete2, 1);
    rb_objc_define_method(rb_cSet, "delete_if", rb_set_delete_if, 0);
    rb_objc_define_method(rb_cSet, "reject!", rb_set_reject_bang, 0);
    rb_objc_define_method(rb_cSet, "each", rb_set_each, 0);
    rb_objc_define_method(rb_cSet, "include?", rb_set_include, 1);
    rb_define_alias(rb_cSet, "member?", "include?");
}
