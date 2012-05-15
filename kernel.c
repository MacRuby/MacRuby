/*
 * MacRuby kernel. This file is compiled into LLVM bitcode and injected into
 * the global module. Some of the functions defined here are inlined in the
 * code MacRuby generates.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2011, Apple Inc. All rights reserved.
 */

#include "macruby_internal.h"
#include "ruby/node.h"
#include "vm.h"
#include "compiler.h"
#include "bridgesupport.h"
#include "id.h"
#include "array.h"
#include "hash.h"
#include "encoding.h"
#include "class.h"
#include "objc.h"

#ifndef PRIMITIVE
#define PRIMITIVE
#endif

PRIMITIVE VALUE
vm_gc_wb(VALUE *slot, VALUE val)
{
    GC_WB_0(slot, val, false);
    return val;
}

PRIMITIVE VALUE
vm_ivar_get(VALUE obj, ID name, void *cache_p)
{
    struct icache *cache = (struct icache *)cache_p;
    VALUE klass = 0;

    if (!SPECIAL_CONST_P(obj)) {
	klass = *(VALUE *)obj;
	if (klass == cache->klass) {
	    if ((unsigned int)cache->slot < ROBJECT(obj)->num_slots) {
		rb_object_ivar_slot_t *slot;
use_slot:
		slot = &ROBJECT(obj)->slots[cache->slot];
		if (slot->name == name) {
		    VALUE val = slot->value;
		    if (val == Qundef) {
			val = Qnil;
		    }
		    return val;
		}
	    }
	    goto find_slot;
	}
    }

    if (cache->slot == SLOT_CACHE_VIRGIN) {
	int slot;
find_slot:
	slot = rb_vm_get_ivar_slot(obj, name, true);
	if (slot >= 0) {
	    cache->klass = *(VALUE *)obj;
	    cache->slot = slot;
	    goto use_slot;
	}
	cache->klass = 0;
	cache->slot = SLOT_CACHE_CANNOT;
    }

    return rb_ivar_get(obj, name);
}

PRIMITIVE void
vm_ivar_set(VALUE obj, ID name, VALUE val, void *cache_p)
{
    struct icache *cache = (struct icache *)cache_p; 
    VALUE klass = 0;

    if (!SPECIAL_CONST_P(obj)) {
	klass = *(VALUE *)obj;
	if (klass == cache->klass) {
	    if ((unsigned int)cache->slot < ROBJECT(obj)->num_slots) {
		rb_object_ivar_slot_t *slot;
use_slot:
		slot = &ROBJECT(obj)->slots[cache->slot];
		if (slot->name == name) {
		    if ((ROBJECT(obj)->basic.flags & FL_FREEZE) == FL_FREEZE) {
			rb_error_frozen("object");
		    }
		    GC_WB(&slot->value, val);
		    return;
		}
	    }
	    goto find_slot;
	}
    }

    if (cache->slot == SLOT_CACHE_VIRGIN) {
	int slot;
find_slot:
	slot = rb_vm_get_ivar_slot(obj, name, true);
	if (slot >= 0) {
	    cache->klass = *(VALUE *)obj;
	    cache->slot = slot;
	    goto use_slot;
	}
	cache->slot = SLOT_CACHE_CANNOT;
    }

    rb_ivar_set(obj, name, val);
}

PRIMITIVE VALUE
vm_cvar_get(VALUE klass, ID id, unsigned char check,
	unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = rb_vm_get_current_class();
	if (k != NULL) {
	    klass = (VALUE)k;
	}
    }
    return rb_cvar_get2(klass, id, check);
}

PRIMITIVE VALUE
vm_cvar_set(VALUE klass, ID id, VALUE val, unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = rb_vm_get_current_class();
	if (k != NULL) {
	    klass = (VALUE)k;
	}
    }
    rb_cvar_set(klass, id, val);
    return val;
}

PRIMITIVE VALUE
vm_get_const(VALUE outer, void *cache_p, ID path, int flags,
	void *outer_stack_p)
{
    struct ccache *cache = (struct ccache *)cache_p;
    rb_vm_outer_t *outer_stack = (rb_vm_outer_t *)outer_stack_p;
    const bool lexical_lookup = (flags & CONST_LOOKUP_LEXICAL);
    const bool dynamic_class = (flags & CONST_LOOKUP_DYNAMIC_CLASS);

    if (dynamic_class && lexical_lookup) {
	rb_vm_outer_t *o = outer_stack;
	while (o != NULL && o->pushed_by_eval) {
	    o = o->outer;
	}
	if (o == NULL) {
            outer = rb_cNSObject;
        }
        else {
	    outer = (VALUE)o->klass;
	}
    }

    VALUE val;
    if (cache->outer == outer && cache->outer_stack == outer_stack
	    && cache->val != Qundef) {
	val = cache->val;
    }
    else {
	val = rb_vm_const_lookup_level(outer, path, lexical_lookup, false,
		outer_stack);
	cache->outer = outer;
	cache->outer_stack = outer_stack;
	cache->val = val;
    }
    return val;
}

PRIMITIVE void 
vm_set_const(VALUE outer, ID id, VALUE obj, unsigned char dynamic_class, void *outer_stack_p)
{
    if (dynamic_class) {
	rb_vm_outer_t *outer_stack = (rb_vm_outer_t *)outer_stack_p;
	if (outer_stack == NULL) {
	    outer = rb_cNSObject;
	}
	else {
	    outer = outer_stack->klass ? (VALUE)outer_stack->klass : Qnil;
	}
    }
    rb_const_set(outer, id, obj);
}

static void __attribute__((noinline))
vm_resolve_args(VALUE **pargv, size_t argv_size, int *pargc, VALUE *args)
{
    unsigned int i, argc = *pargc, real_argc = 0, j = 0;
    VALUE *argv = *pargv;
    bool splat_arg_follows = false;
    for (i = 0; i < argc; i++) {
	VALUE arg = args[j++];
	if (arg == SPLAT_ARG_FOLLOWS) {
	    splat_arg_follows = true;
	    i--;
	}
	else {
	    if (splat_arg_follows) {
		VALUE ary = rb_check_convert_type(arg, T_ARRAY, "Array",
			"to_a");
		if (NIL_P(ary)) {
		    ary = rb_ary_new4(1, &arg);
		}
		int count = RARRAY_LEN(ary);
		if (real_argc + count >= argv_size) {
		    const size_t new_argv_size = real_argc + count + 100;
		    VALUE *new_argv = (VALUE *)xmalloc_ptrs(sizeof(VALUE)
			    * new_argv_size);
		    memcpy(new_argv, argv, sizeof(VALUE) * argv_size);
		    argv = new_argv;
		    argv_size = new_argv_size;
		}
		int j;
		for (j = 0; j < count; j++) {
		    argv[real_argc++] = RARRAY_AT(ary, j);
		}
		splat_arg_follows = false;
	    }
	    else {
		if (real_argc >= argv_size) {
		    const size_t new_argv_size = real_argc + 100;
		    VALUE *new_argv = (VALUE *)xmalloc_ptrs(sizeof(VALUE)
			    * new_argv_size);
		    memcpy(new_argv, argv, sizeof(VALUE) * argv_size);
		    argv = new_argv;
		    argv_size = new_argv_size;
		}
		argv[real_argc++] = arg;
	    }
	}
    }
    *pargv = argv;
    *pargc = real_argc;
}

static inline VALUE
vm_class_of(VALUE obj)
{
    // TODO: separate the const bits of CLASS_OF to make sure they will get
    // reduced by the optimizer.
    return CLASS_OF(obj);
}

PRIMITIVE VALUE
vm_dispatch(VALUE top, VALUE self, void *sel, void *block, unsigned char opt,
	int argc, VALUE *argv)
{
    if (opt & DISPATCH_SUPER) {
	if (sel == 0) {
	    rb_raise(rb_eNoMethodError, "super called outside of method");
	}
    }

    VALUE buf[100];
    if (opt & DISPATCH_SPLAT) {
	if (argc == 1 && !SPECIAL_CONST_P(argv[1])
		&& *(VALUE *)argv[1] == rb_cRubyArray) {
	    argc = RARY(argv[1])->len;
	    argv = rary_ptr(argv[1]);
	}
	else {
	    VALUE *new_argv = buf;
	    vm_resolve_args(&new_argv, 100, &argc, argv);
	    argv = new_argv;
	}
	if (argc == 0) {
	    const char *selname = sel_getName((SEL)sel);
	    const size_t selnamelen = strlen(selname);
	    if (selname[selnamelen - 1] == ':') {
		// Because
		//   def foo; end; foo(*[])
		// creates foo but dispatches foo:.
		char buf[100];
		strncpy(buf, selname, sizeof buf);
		buf[selnamelen - 1] = '\0';
		sel = sel_registerName(buf);
	    }
	}
    }

    void *vm = rb_vm_current_vm();
    VALUE klass = vm_class_of(self);
    return rb_vm_call0(vm, top, self, (Class)klass, (SEL)sel,
	    (rb_vm_block_t *)block, opt, argc, argv);
}

PRIMITIVE VALUE
vm_yield_args(int argc, unsigned char opt, VALUE *argv)
{
    VALUE buf[100];
    if (opt & DISPATCH_SPLAT) {
	if (argc == 1 && !SPECIAL_CONST_P(argv[1])
		&& *(VALUE *)argv[1] == rb_cRubyArray) {
	    argc = RARY(argv[1])->len;
	    argv = rary_ptr(argv[1]);
	}
	else {
	    VALUE *new_argv = buf;
	    vm_resolve_args(&new_argv, 100, &argc, argv);
	    argv = new_argv;
	}
    }

    void *vm = rb_vm_current_vm();
    return rb_vm_yield_args(vm, argc, argv);
}

PRIMITIVE VALUE
vm_get_broken_value(void)
{
    return rb_vm_get_broken_value(rb_vm_current_vm());
}

PRIMITIVE VALUE
vm_returned_from_block(int id)
{
    return rb_vm_returned_from_block(rb_vm_current_vm(), id);
}

PRIMITIVE void
vm_release_ownership(VALUE obj)
{
    rb_vm_release_ownership(obj);
}

PRIMITIVE void *
vm_get_block(VALUE obj)
{
    if (obj == Qnil) {
	return NULL;
    }

    VALUE proc = rb_check_convert_type(obj, T_DATA, "Proc", "to_proc");
    if (NIL_P(proc)) {
	rb_raise(rb_eTypeError,
		"wrong argument type %s (expected Proc)",
		rb_obj_classname(obj));
    }
    return rb_proc_get_block(proc);
}

// Only numeric immediates have their lsb at 1.
#define NUMERIC_IMM_P(x) ((x & 0x1) == 0x1)

#define IMM2DBL(x) (FIXFLOAT_P(x) ? FIXFLOAT2DBL(x) : FIX2LONG(x))

PRIMITIVE VALUE
vm_fast_plus(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    const long res = FIX2LONG(left) + FIX2LONG(right);
	    if (FIXABLE(res)) {
		return LONG2FIX(res);
	    }
	}
	else {
	    const double res = IMM2DBL(left) + IMM2DBL(right);
	    return DBL2FIXFLOAT(res);
	}
    }
    return vm_dispatch(0, left, selPLUS, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_minus(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    const long res = FIX2LONG(left) - FIX2LONG(right);
	    if (FIXABLE(res)) {
		return LONG2FIX(res);
	    }
	}
	else {
	    const double res = IMM2DBL(left) - IMM2DBL(right);
	    return DBL2FIXFLOAT(res);
	}
    }
    return vm_dispatch(0, left, selMINUS, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_mult(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    const long a = FIX2LONG(left);
	    if (a == 0) {
		return left;
	    }
	    const long b = FIX2LONG(right);
	    const long res = a * b;
	    if (FIXABLE(res) && res / a == b) {
		return LONG2FIX(res);
	    }
	    else {
		return rb_big_mul(rb_int2big(a), rb_int2big(b));
	    }
	}
	else {
	    const double res = IMM2DBL(left) * IMM2DBL(right);
	    return DBL2FIXFLOAT(res);
	}
    }
    return vm_dispatch(0, left, selMULT, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_div(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    const long x = FIX2LONG(left);
	    const long y = FIX2LONG(right);
	    if (y != 0) {
		long res = x / y;
		if (((x < 0 && y >= 0) || (x >= 0 && y < 0))
			&& (x % y) != 0) {
		    res--;
		}
		if (FIXABLE(res)) {
		    return LONG2FIX(res);
		}
	    }
	}
	else {
	    const double res = IMM2DBL(left) / IMM2DBL(right);
	    return DBL2FIXFLOAT(res);
	}
    }
    return vm_dispatch(0, left, selDIV, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_mod(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    const long x = FIX2LONG(left);
	    const long y = FIX2LONG(right);
	    long div, mod;

	    if (y == 0) {
		rb_num_zerodiv();
	    }
	    if (y < 0) {
		if (x < 0) {
		    div = -x / -y;
		}
		else {
		    div = - (x / -y);
		}
	    }
	    else {
		if (x < 0) {
		    div = - (-x / y);
		}
		else {
		    div = x / y;
		}
	    }
	    mod = x - div*y;
	    if ((mod < 0 && y > 0) || (mod > 0 && y < 0)) {
		mod += y;
		div -= 1;
	    }
	    return LONG2FIX(mod);
	}
	else if (FIXFLOAT_P(left) && FIXFLOAT_P(right)) {
	    const double x = IMM2DBL(left);
	    const double y = IMM2DBL(right);
	    double div, mod;

#ifdef HAVE_FMOD
	    mod = fmod(x, y);
#else
	    double z;
	    modf(x/y, &z);
	    mod = x - z * y;
#endif
	    div = (x - mod) / y;
	    if (y * mod < 0) {
		mod += y;
		div -= 1.0;
	    }
	    return DBL2FIXFLOAT(mod);
	}
    }
    return vm_dispatch(0, left, selMOD, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_lt(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    return FIX2LONG(left) < FIX2LONG(right) ? Qtrue : Qfalse;
	}
	else {
	    return IMM2DBL(left) < IMM2DBL(right) ? Qtrue : Qfalse;
	}
    }
    return vm_dispatch(0, left, selLT, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_le(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    return FIX2LONG(left) <= FIX2LONG(right) ? Qtrue : Qfalse;
	}
	else {
	    return IMM2DBL(left) <= IMM2DBL(right) ? Qtrue : Qfalse;
	}
    }
    return vm_dispatch(0, left, selLE, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_gt(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    return FIX2LONG(left) > FIX2LONG(right) ? Qtrue : Qfalse;
	}
	else {
	    return IMM2DBL(left) > IMM2DBL(right) ? Qtrue : Qfalse;
	}
    }
    return vm_dispatch(0, left, selGT, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_ge(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    return FIX2LONG(left) >= FIX2LONG(right) ? Qtrue : Qfalse;
	}
	else {
	    return IMM2DBL(left) >= IMM2DBL(right) ? Qtrue : Qfalse;
	}
    }
    return vm_dispatch(0, left, selGE, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_eq(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0) {
	if (NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	    if (FIXNUM_P(left) && FIXNUM_P(right)) {
		return FIX2LONG(left) == FIX2LONG(right) ? Qtrue : Qfalse;
	    }
	    else {
		return IMM2DBL(left) == IMM2DBL(right) ? Qtrue : Qfalse;
	    }
	}
	if (left == Qtrue || left == Qfalse) {
	    return left == right ? Qtrue : Qfalse;
	}
	// TODO: opt for non-immediate types
    }
    return vm_dispatch(0, left, selEq, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_eqq(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0) {
	if (NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	    if (FIXNUM_P(left) && FIXNUM_P(right)) {
		return FIX2LONG(left) == FIX2LONG(right) ? Qtrue : Qfalse;
	    }
	    else {
		return IMM2DBL(left) == IMM2DBL(right) ? Qtrue : Qfalse;
	    }
	}
	if (left == Qtrue || left == Qfalse) {
	    return left == right ? Qtrue : Qfalse;
	}
	// TODO: opt for non-immediate types
    }
    return vm_dispatch(0, left, selEqq, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_neq(VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0) {
	if (NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	    if (FIXNUM_P(left) && FIXNUM_P(right)) {
		return FIX2LONG(left) != FIX2LONG(right) ? Qtrue : Qfalse;
	    }
	    else {
		return IMM2DBL(left) != IMM2DBL(right) ? Qtrue : Qfalse;
	    }
	} 
	if (left == Qtrue || left == Qfalse) {
	    return left != right ? Qtrue : Qfalse;
	}
	// TODO: opt for non-immediate types
    }
    return vm_dispatch(0, left, selNeq, NULL, 0, 1, &right);
}

PRIMITIVE VALUE
vm_fast_aref(VALUE obj, VALUE other, unsigned char overriden)
{
    if (overriden == 0 && !SPECIAL_CONST_P(obj)) {
	VALUE klass = *(VALUE *)obj;
	if (klass == rb_cRubyArray) {
	    if (FIXNUM_P(other)) {
		return rary_entry(obj, FIX2LONG(other));
	    }
	}
	else if (klass == rb_cRubyHash) {
	    return rhash_aref(obj, 0, other);
	}
    }
    return vm_dispatch(0, obj, selAREF, NULL, 0, 1, &other);
}

PRIMITIVE VALUE
vm_fast_aset(VALUE obj, VALUE other1, VALUE other2, unsigned char overriden)
{
    if (overriden == 0 && !SPECIAL_CONST_P(obj)) {
	VALUE klass = *(VALUE *)obj;
	if (klass == rb_cRubyArray) {
	    if (FIXNUM_P(other1)) {
		rary_store(obj, FIX2LONG(other1), other2);
		return other2;
	    }
	}
	else if (klass == rb_cRubyHash) {
	    return rhash_aset(obj, 0, other1, other2);
	}
    }
    VALUE args[] = {other1, other2};
    return vm_dispatch(0, obj, selASET, NULL, 0, 2, args);
}

PRIMITIVE VALUE
vm_fast_shift(VALUE obj, VALUE other, unsigned char overriden)
{
    if (overriden == 0 && !SPECIAL_CONST_P(obj)) {
	VALUE klass = *(VALUE *)obj;
	if (klass == rb_cRubyArray) {
	    rary_modify(obj);
	    rary_push(obj, other);
	    return obj;
	}
	else if (klass == rb_cRubyString) {
	    return rstr_concat(obj, 0, other);
	}
    }
    return vm_dispatch(0, obj, selLTLT, NULL, 0, 1, &other);
}

PRIMITIVE VALUE
vm_when_splat(unsigned char overriden, VALUE comparedTo, VALUE splat)
{
    VALUE ary = rb_check_convert_type(splat, T_ARRAY, "Array", "to_a");
    if (NIL_P(ary)) {
	ary = rb_ary_new4(1, &splat);
    }
    long i, count = RARRAY_LEN(ary);
    for (i = 0; i < count; i++) {
	VALUE o = RARRAY_AT(ary, i);
	if (RTEST(vm_fast_eqq(o, comparedTo, overriden))) {
	    return Qtrue;
	}
    }
    return Qfalse;
}

PRIMITIVE void
vm_set_current_scope(VALUE mod, int scope)
{
    rb_vm_set_current_scope(mod, (rb_vm_scope_t)scope);
}

PRIMITIVE VALUE
vm_ocval_to_rval(void *ocval)
{
    return OC2RB(ocval);
}

PRIMITIVE VALUE
vm_char_to_rval(char c)
{
    return INT2FIX(c);
}

PRIMITIVE VALUE
vm_uchar_to_rval(unsigned char c)
{
    return INT2FIX(c);
}

PRIMITIVE VALUE
vm_short_to_rval(short c)
{
    return INT2FIX(c);
}

PRIMITIVE VALUE
vm_ushort_to_rval(unsigned short c)
{
    return INT2FIX(c);
}

PRIMITIVE VALUE
vm_int_to_rval(int c)
{
    return INT2FIX(c);
}

PRIMITIVE VALUE
vm_uint_to_rval(unsigned int c)
{
    return INT2FIX(c);
}

PRIMITIVE VALUE
vm_long_to_rval(long l)
{
    return LONG2NUM(l);
}

PRIMITIVE VALUE
vm_ulong_to_rval(unsigned long l)
{
    return ULONG2NUM(l);
}

PRIMITIVE VALUE
vm_long_long_to_rval(long long l)
{
    return LL2NUM(l);
}

PRIMITIVE VALUE
vm_ulong_long_to_rval(unsigned long long l)
{
    return ULL2NUM(l);
}

PRIMITIVE VALUE
vm_float_to_rval(float f)
{
    return DOUBLE2NUM(f);
}

PRIMITIVE VALUE
vm_double_to_rval(double d)
{
    return DOUBLE2NUM(d);
}

PRIMITIVE VALUE
vm_sel_to_rval(void *sel)
{
    return sel == 0 ? Qnil : ID2SYM(rb_intern(sel_getName((SEL)sel)));
}

PRIMITIVE VALUE
vm_charptr_to_rval(const char *ptr)
{
    return ptr == NULL ? Qnil : rb_str_new2(ptr);
}

PRIMITIVE void
vm_rval_to_ocval(VALUE rval, void **ocval)
{
    *ocval = rval == Qnil ? NULL : RB2OC(rval);
}

PRIMITIVE void
vm_rval_to_bool(VALUE rval, BOOL *ocval)
{
    if (rval == Qfalse || rval == Qnil) {
	*ocval = NO;
    }
    else {
	// All other types should be converted as true, to follow the Ruby
	// semantics (where for example any integer is always true, even 0).
	*ocval = YES;
    }
}

static inline const char *
rval_to_c_str(VALUE rval)
{
    if (NIL_P(rval)) {
	return NULL;
    }
    else {
	if (CLASS_OF(rval) == rb_cSymbol) {
	    return rb_sym2name(rval);
	}
	if (rb_obj_is_kind_of(rval, rb_cPointer)) {
	    return (const char *)rb_pointer_get_data(rval, "^c");
	}
	return StringValueCStr(rval);
    }
}

PRIMITIVE void
vm_rval_to_sel(VALUE rval, void **ocval)
{
    const char *cstr = rval_to_c_str(rval);
    *(SEL *)ocval = cstr == NULL ? NULL : sel_registerName(cstr);
}

PRIMITIVE void
vm_rval_to_charptr(VALUE rval, const char **ocval)
{
    *ocval = rval_to_c_str(rval);
}

static inline long
bool_to_fix(VALUE rval)
{
    if (rval == Qtrue) {
	return INT2FIX(1);
    }
    if (rval == Qfalse) {
	return INT2FIX(0);
    }
    return rval;
}

static inline long
rval_to_long(VALUE rval)
{
   return NUM2LONG(rb_Integer(bool_to_fix(rval))); 
}

static inline long long
rval_to_long_long(VALUE rval)
{
    return NUM2LL(rb_Integer(bool_to_fix(rval)));
}

static inline unsigned long long
rval_to_ulong_long(VALUE rval)
{
    return NUM2ULL(rb_Integer(bool_to_fix(rval)));
}

static inline double
rval_to_double(VALUE rval)
{
    return RFLOAT_VALUE(rb_Float(bool_to_fix(rval)));
}

PRIMITIVE void
vm_rval_to_char(VALUE rval, char *ocval)
{
    if (TYPE(rval) == T_STRING && RSTRING_LEN(rval) == 1) {
	*ocval = (char)RSTRING_PTR(rval)[0];
    }
    else {
	*ocval = (char)rval_to_long(rval);
    }
}

PRIMITIVE void
vm_rval_to_uchar(VALUE rval, unsigned char *ocval)
{
    if (TYPE(rval) == T_STRING && RSTRING_LEN(rval) == 1) {
	*ocval = (unsigned char)RSTRING_PTR(rval)[0];
    }
    else {
	*ocval = (unsigned char)rval_to_long(rval);
    }
}

PRIMITIVE void
vm_rval_to_short(VALUE rval, short *ocval)
{
    *ocval = (short)rval_to_long(rval);
}

PRIMITIVE void
vm_rval_to_ushort(VALUE rval, unsigned short *ocval)
{
    *ocval = (unsigned short)rval_to_long(rval);
}

PRIMITIVE void
vm_rval_to_int(VALUE rval, int *ocval)
{
    *ocval = (int)rval_to_long(rval);
}

PRIMITIVE void
vm_rval_to_uint(VALUE rval, unsigned int *ocval)
{
    *ocval = (unsigned int)rval_to_long(rval);
}

PRIMITIVE void
vm_rval_to_long(VALUE rval, long *ocval)
{
    *ocval = (long)rval_to_long(rval);
}

PRIMITIVE void
vm_rval_to_ulong(VALUE rval, unsigned long *ocval)
{
    *ocval = (unsigned long)rval_to_long(rval);
}

PRIMITIVE void
vm_rval_to_long_long(VALUE rval, long long *ocval)
{
    *ocval = rval_to_long_long(rval);
}

PRIMITIVE void
vm_rval_to_ulong_long(VALUE rval, unsigned long long *ocval)
{
    *ocval = rval_to_ulong_long(rval);
}

PRIMITIVE void
vm_rval_to_double(VALUE rval, double *ocval)
{
    *ocval = (double)rval_to_double(rval);
}

PRIMITIVE void
vm_rval_to_float(VALUE rval, float *ocval)
{
    *ocval = (float)rval_to_double(rval);
}

PRIMITIVE void *
vm_rval_to_cptr(VALUE rval, const char *type, void **cptr)
{
    if (NIL_P(rval)) {
	*cptr = NULL;
    }
    else {
	if (TYPE(rval) == T_ARRAY
	    || rb_boxed_is_type(CLASS_OF(rval), type + 1)) {
	    // A convenience helper so that the user can pass a Boxed or an 
	    // Array object instead of a Pointer to the object.
	    rval = rb_pointer_new2(type + 1, rval);
	}
	*cptr = rb_pointer_get_data(rval, type);
    }
    return *cptr;
}

PRIMITIVE VALUE
vm_to_a(VALUE obj)
{
    VALUE ary = rb_check_convert_type(obj, T_ARRAY, "Array", "to_a");
    if (NIL_P(ary)) {
	ary = rb_ary_new4(1, &obj);
    }
    return ary;
}

PRIMITIVE VALUE
vm_to_ary(VALUE obj)
{
    VALUE ary = rb_check_convert_type(obj, T_ARRAY, "Array", "to_ary");
    if (NIL_P(ary)) {
	ary = rb_ary_new4(1, &obj);
    }
    return ary;
}

PRIMITIVE VALUE
vm_ary_cat(VALUE ary, VALUE obj)
{
    VALUE ary2 = rb_check_convert_type(obj, T_ARRAY, "Array", "to_a");
    if (!NIL_P(ary2)) {
	rb_ary_concat(ary, ary2);
    }
    else {
	rb_ary_push(ary, obj);
    }
    return ary;
}

PRIMITIVE VALUE
vm_ary_dup(VALUE ary)
{
    return rb_ary_dup(ary);
}

PRIMITIVE VALUE
vm_ary_check(VALUE obj, int size)
{
    VALUE ary = rb_check_convert_type(obj, T_ARRAY, "Array", "to_ary");
    if (NIL_P(ary)) {
	rb_raise(rb_eTypeError, "expected Array");
    }
    if (RARRAY_LEN(ary) != size) {
	rb_raise(rb_eArgError, "expected Array of size %d, got %ld",
		size, RARRAY_LEN(ary));
    }
    return ary;
}

PRIMITIVE VALUE
vm_ary_entry(VALUE ary, int i)
{
    return rb_ary_entry(ary, i);
}

PRIMITIVE long
vm_ary_length(VALUE ary)
{
    return RARRAY_LEN(ary);
}

PRIMITIVE const VALUE *
vm_ary_ptr(VALUE ary)
{
    return RARRAY_PTR(ary);
}

PRIMITIVE VALUE
vm_rary_new(int argc, VALUE *argv)
{
    VALUE ary = rb_ary_new2(argc);
    int i;
    for (i = 0; i < argc; i++) {
	rary_elt_set(ary, i, argv[i]);
    }
    RARY(ary)->len = argc;
    return ary;
}

PRIMITIVE VALUE
vm_rhash_new(void)
{
    return rb_hash_new();
}

PRIMITIVE void
vm_rhash_store(VALUE hash, VALUE key, VALUE obj)
{
    rhash_store(hash, key, obj);
}

PRIMITIVE VALUE
vm_masgn_get_elem_before_splat(VALUE ary, int offset)
{
    if (offset < RARRAY_LEN(ary)) {
	return RARRAY_AT(ary, offset);
    }
    return Qnil;
}

PRIMITIVE VALUE
vm_masgn_get_elem_after_splat(VALUE ary, int before_splat_count,
	int after_splat_count, int offset)
{
    const int len = RARRAY_LEN(ary);
    if (len < before_splat_count + after_splat_count) {
	offset += before_splat_count;
	if (offset < len) {
	    return RARRAY_AT(ary, offset);
	}
    }
    else {
	offset += len - after_splat_count;
	return RARRAY_AT(ary, offset);
    }
    return Qnil;
}

PRIMITIVE VALUE
vm_masgn_get_splat(VALUE ary, int before_splat_count, int after_splat_count)
{
    const int len = RARRAY_LEN(ary);
    if (len > before_splat_count + after_splat_count) {
	return rb_ary_subseq(ary, before_splat_count,
		len - before_splat_count - after_splat_count);
    }
    else {
	return rb_ary_new();
    }
}

// Support for C-level blocks.
// Following the ABI specifications as documented in the
// BlockImplementation.txt file of LLVM.

struct ruby_block_descriptor {
    unsigned long int reserved;
    unsigned long int block_size;
};

struct ruby_block_literal {
    void *isa;
    int flags;
    int reserved;
    void *imp;
    struct ruby_block_descriptor *descriptor;
    VALUE ruby_proc;
};

static struct ruby_block_descriptor ruby_block_descriptor_value = {
    0, sizeof(struct ruby_block_literal)
};

extern void *_NSConcreteAutoBlock[32];

#define __MR_BLOCK_IS_GC		(1 << 27)
#define __MR_BLOCK_HAS_DESCRIPTOR 	(1 << 29)

PRIMITIVE void
vm_init_c_block(struct ruby_block_literal *b, void *imp, VALUE proc)
{
    b->isa = &_NSConcreteAutoBlock;
    b->flags = __MR_BLOCK_IS_GC | __MR_BLOCK_HAS_DESCRIPTOR;
    b->reserved = 0;
    b->imp = imp;
    b->descriptor = &ruby_block_descriptor_value;
    GC_WB(&b->ruby_proc, proc);
}

PRIMITIVE VALUE
vm_ruby_block_literal_proc(struct ruby_block_literal *b)
{
    return b->ruby_proc;
}
