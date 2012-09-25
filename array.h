/*
 * MacRuby Array.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2011, Apple Inc. All rights reserved.
 */

#ifndef __ARRAY_H_
#define __ARRAY_H_

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct RArray {
    struct RBasic basic;
    size_t beg;
    size_t len;
    size_t cap;
    VALUE *elements;
} rb_ary_t;

#define ARY_MAX_SIZE (LONG_MAX / (int)sizeof(VALUE))
#define RARY(x) ((rb_ary_t *)x)

static inline bool
rb_klass_is_rary(VALUE klass)
{
    do {
	if (klass == rb_cRubyArray) {
	    return true;
	}
	if (klass == rb_cNSArray) {
	    return false;
	}
	klass = RCLASS_SUPER(klass);
    }
    while (klass != 0);
    return false;
}

#define IS_RARY(x) (rb_klass_is_rary(*(VALUE *)x))

static inline void
rary_modify(VALUE ary)
{
    const long mask = RBASIC(ary)->flags;
    if ((mask & FL_FREEZE) == FL_FREEZE) {
        rb_raise(rb_eRuntimeError, "can't modify frozen/immutable array");
    }
    if ((mask & FL_UNTRUSTED) != FL_UNTRUSTED) {
        if (rb_safe_level() >= 4) {
            rb_raise(rb_eSecurityError, "Insecure: can't modify array");
        }
    }
}

static inline VALUE
rary_elt(VALUE ary, size_t idx)
{
    //assert(idx < RARY(ary)->len);
    return RARY(ary)->elements[RARY(ary)->beg + idx];	
}

static inline void
rary_elt_set(VALUE ary, size_t idx, VALUE item)
{
    //assert(idx < ary->len);
    GC_WB(&RARY(ary)->elements[RARY(ary)->beg + idx], item);
}

static inline VALUE *
rary_ptr(VALUE ary)
{
    return &RARY(ary)->elements[RARY(ary)->beg];
}

static inline VALUE
rary_entry(VALUE ary, long offset)
{
    const long n = RARY(ary)->len;
    if (n == 0) {
	return Qnil;
    }
    if (offset < 0) {
	offset += n;
    }
    if (offset < 0 || n <= offset) {
	return Qnil;
    }
    return rary_elt(ary, offset);
}

void rary_reserve(VALUE ary, size_t newlen);

static inline void
rary_store(VALUE ary, long idx, VALUE item)
{
    if (idx < 0) {
        const long len = RARY(ary)->len;
        idx += len;
        if (idx < 0) {
            rb_raise(rb_eIndexError, "index %ld out of array",
                    idx - len);
        }
    }
    else if (idx >= ARY_MAX_SIZE) {
	rb_raise(rb_eIndexError, "index %ld too big", idx);
    }

    rary_modify(ary);
    size_t uidx = (size_t)idx;
    if (uidx >= RARY(ary)->len) {
        rary_reserve(ary, uidx + 1);
	size_t i;
        for (i = RARY(ary)->len; i < uidx + 1; i++) {
            rary_elt_set(ary, i, Qnil);
        }
        RARY(ary)->len = uidx + 1;
    }
    rary_elt_set(ary, uidx, item);
}

static inline void
rary_push(VALUE ary, VALUE item)
{
    rary_reserve(ary, RARY(ary)->len + 1);
    rary_elt_set(ary, RARY(ary)->len, item);
    RARY(ary)->len++;
}

static inline VALUE
to_ary(VALUE ary)
{
    return rb_convert_type(ary, T_ARRAY, "Array", "to_ary");
}

VALUE rary_dup(VALUE ary, SEL sel);
VALUE rary_clear(VALUE ary, SEL sel);
VALUE rary_reverse_bang(VALUE ary, SEL sel);
VALUE rary_includes(VALUE ary, SEL sel, VALUE item);
VALUE rary_delete(VALUE ary, SEL sel, VALUE item);
VALUE rary_delete_at(VALUE ary, SEL sel, VALUE pos);
VALUE rary_pop(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_shift(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_aref(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_plus(VALUE x, SEL sel, VALUE y);
VALUE rary_push_m(VALUE ary, SEL sel, VALUE item);
VALUE rary_concat_m(VALUE x, SEL sel, VALUE y);
VALUE rary_last(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_unshift(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_each(VALUE ary, SEL sel);
VALUE rary_sort(VALUE ary, SEL sel);
VALUE rary_sort_bang(VALUE ary, SEL sel);
VALUE rary_subseq(VALUE ary, long beg, long len);
void rary_insert(VALUE ary, long idx, VALUE val);
bool rary_eql_fast(rb_ary_t *ary1, rb_ary_t *ary2);

// Shared implementations.
VALUE rary_join(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_hash(VALUE ary, SEL sel);
VALUE rary_zip(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_transpose(VALUE ary, SEL sel);
VALUE rary_fill(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_cmp(VALUE ary1, SEL sel, VALUE ary2);
VALUE rary_assoc(VALUE ary, SEL sel, VALUE key);
VALUE rary_rassoc(VALUE ary, SEL sel, VALUE value);
VALUE rary_flatten(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_flatten_bang(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_product(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_combination(VALUE ary, SEL sel, VALUE num);
VALUE rary_permutation(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_cycle(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_sample(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_diff(VALUE ary1, SEL sel, VALUE ary2);
VALUE rary_and(VALUE ary1, SEL sel, VALUE ary2);
VALUE rary_or(VALUE ary1, SEL sel, VALUE ary2);
VALUE rary_rotate(VALUE ary, SEL sel, int argc, VALUE *argv);
VALUE rary_rotate_bang(VALUE ary, SEL sel, int argc, VALUE *argv);

VALUE rb_ary_make_hash(VALUE ary1, VALUE ary2);
VALUE rb_ary_make_hash_by(VALUE ary);

unsigned long rb_ary_hash(VALUE ary);
VALUE rb_f_array(VALUE, SEL, VALUE);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // __ARRAY_H_
