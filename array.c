/* 
 * MacRuby implementation of Ruby 1.9's array.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2008, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include "ruby/ruby.h"
#include "ruby/util.h"
#include "ruby/st.h"
#include "id.h"
#include "objc.h"
#include "ruby/node.h"
#include "vm.h"

VALUE rb_cArray;
VALUE rb_cCFArray;
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
VALUE rb_cNSArray0;
#endif
VALUE rb_cNSArray;
VALUE rb_cNSMutableArray;
VALUE rb_cRubyArray;

#define ARY_DEFAULT_SIZE 16

typedef struct {
    struct RBasic basic;
    size_t beg;
    size_t len;
    size_t cap;
    VALUE *elements;
} rb_ary_t;

// RubyArray primitives.

static VALUE
rary_elt(rb_ary_t *ary, size_t idx)
{
//    assert(idx < ary->len);
    return ary->elements[ary->beg + idx];	
}

static void
rary_replace(rb_ary_t *ary, size_t idx, VALUE item)
{
//    assert(idx < ary->len);
    GC_WB(&ary->elements[ary->beg + idx], item);
}

static VALUE *
rary_ptr(rb_ary_t *ary)
{
    return &ary->elements[ary->beg];
}

static void
rary_reserve(rb_ary_t *ary, size_t newlen)
{
    if (ary->beg + newlen > ary->cap) {
	if (ary->beg > 0) {
	    if (ary->beg > newlen) {
		newlen = 0;
	    }
	    else {
		newlen -= ary->beg;
	    }
	    for (size_t i = 0; i < ary->len; i++) {
		GC_WB(&ary->elements[i], ary->elements[ary->beg + i]);
	    }
	    ary->beg = 0;
	}
	if (newlen > ary->cap) {
	    if (ary->cap > 0) {
		newlen *= 2;
	    }
#if MAC_OS_X_VERSION_MAX_ALLOWED < 1060
	    VALUE *new_elements = (VALUE *)xmalloc(sizeof(VALUE) * newlen);
	    for (size_t i = 0; i < ary->len; i++) {
		GC_WB(&new_elements[i], ary->elements[i]);
	    }
	    GC_WB(&ary->elements, new_elements);
#else
//printf("xrealloc %p (%ld -> %ld)\n", ary, ary->cap, newlen);
	    VALUE *new_elements = xrealloc(ary->elements, sizeof(VALUE) * newlen);
	    if (new_elements != ary->elements) {
		GC_WB(&ary->elements, new_elements);
	    }
#endif
	    ary->cap = newlen;
	}
    }
}

static void
rary_append(rb_ary_t *ary, VALUE item)
{
    rary_reserve(ary, ary->len + 1);
    rary_replace(ary, ary->len, item);
    ary->len++;
}

static void
rary_insert(rb_ary_t *ary, size_t idx, VALUE item)
{
    assert(idx <= ary->len);
    if (idx < ary->len) {
	rary_reserve(ary, ary->len + 1);
	for (size_t i = ary->len; i > idx; i--) {
	    rary_replace(ary, i, rary_elt(ary, i - 1));
	}
	rary_replace(ary, idx, item);
	ary->len++;
    }
    else {
	rary_append(ary, item);
    }
}

static VALUE
rary_erase(rb_ary_t *ary, size_t idx, size_t len)
{
    assert(idx + len <= ary->len);
    VALUE item = rary_elt(ary, idx);
    if (idx == 0) {
	for (size_t i = 0; i < len; i++) {
	    rary_replace(ary, i, Qnil);
	}
	if (len < ary->len) {
	    ary->beg += len;
	}
	else {
	    ary->beg = 0;
	}
    }
    else {
	for (size_t i = idx; i < ary->len - len; i++) {
	    rary_replace(ary, i, rary_elt(ary, i + len));
	}
	for (size_t i = 0; i < len; i++) {
	    rary_replace(ary, ary->len - i - 1, Qnil);
	}
    }
    ary->len -= len;
    return item;
}

static void
rary_store(rb_ary_t *ary, size_t idx, VALUE item)
{
    if (idx >= ary->len) {
	rary_reserve(ary, idx + 1);
	for (size_t i = ary->len; i < idx + 1; i++) {
	    rary_replace(ary, i, Qnil);
	}
	ary->len = idx + 1;
    }
    rary_replace(ary, idx, item);
}

static void
rary_resize(rb_ary_t *ary, size_t newlen)
{
    if (newlen > ary->cap) {
	rary_reserve(ary, newlen);
    }
    for (size_t i = ary->len; i < newlen; i++) {
	rary_replace(ary, i, Qnil);
    }
    ary->len = newlen;
}

static void
rary_concat(rb_ary_t *ary, rb_ary_t *other, size_t beg, size_t len)
{
    rary_reserve(ary, ary->len + len);
    for (size_t i = 0; i < len; i++) {
	rary_replace(ary, i + ary->len, rary_elt(other, beg + i));
    }
    ary->len += len;
}

static void
rary_reverse(rb_ary_t *ary)
{
    if (ary->len > 1) {
	for (size_t i = 0; i < ary->len / 2; i++) {
	    const size_t j = ary->len - i - 1;
	    VALUE elem = rary_elt(ary, i);
	    rary_replace(ary, i, rary_elt(ary, j));
	    rary_replace(ary, j, elem);
	}
    }
}

static void
rary_clear(rb_ary_t *ary)
{
    memset(ary->elements, 0, sizeof(VALUE) * ary->len);
    ary->len = 0;
}

static VALUE
rb_equal_fast(VALUE x, VALUE y)
{
    if (x == y) {
	return Qtrue;
    }
    if (SPECIAL_CONST_P(x) && SPECIAL_CONST_P(y) && TYPE(x) == TYPE(y)) {
	return Qfalse;
    }
    if (SYMBOL_P(x)) {
	return x == y ? Qtrue : Qfalse;
    }
    return rb_equal(x, y);
}

#define NOT_FOUND LONG_MAX

static size_t
rary_index_of_item(rb_ary_t *ary, size_t origin, VALUE item)
{
    assert(origin < ary->len);
    for (size_t i = origin; i < ary->len; i++) {
	VALUE item2 = rary_elt(ary, i);
	if (rb_equal_fast(item, item2) == Qtrue) {
	    return i;
	}
    }
    return NOT_FOUND;
}

#define IS_RARY(x) (*(VALUE *)x == rb_cRubyArray)
#define RARY(x) ((rb_ary_t *)x)

void
rb_mem_clear(register VALUE *mem, register long size)
{
    while (size--) {
	*mem++ = Qnil;
    }
}

static inline void
__rb_ary_modify(VALUE ary)
{
    long mask;
    if (IS_RARY(ary)) {
	mask = RBASIC(ary)->flags;
    }
    else {
#ifdef __LP64__
	mask = RCLASS_RC_FLAGS(ary);
#else
	mask = rb_objc_flag_get_mask((void *)ary);
#endif
	if (RARRAY_IMMUTABLE(ary)) {
	    mask |= FL_FREEZE;
	}
    }
    if ((mask & FL_FREEZE) == FL_FREEZE) {
	rb_raise(rb_eRuntimeError, "can't modify frozen/immutable array");
    }
    if ((mask & FL_TAINT) == FL_TAINT && rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "Insecure: can't modify array");
    }
}

#define rb_ary_modify(ary) \
    do { \
	if (!IS_RARY(ary) || RBASIC(ary)->flags != 0) { \
	    __rb_ary_modify(ary); \
	} \
    } \
    while (0)

extern void _CFArraySetCapacity(CFMutableArrayRef array, CFIndex cap);

static inline void
rb_ary_set_capacity(VALUE ary, long len)
{
    if (RARRAY_LEN(ary) < len) {
	if (IS_RARY(ary)) {
	    rary_reserve(RARY(ary), len);
	}
	else {
	    _CFArraySetCapacity((CFMutableArrayRef)ary, len);
	}
    }
}

VALUE
rb_ary_freeze(VALUE ary)
{
    return rb_obj_freeze(ary);
}

/*
 *  call-seq:
 *     array.frozen?  -> true or false
 *
 *  Return <code>true</code> if this array is frozen (or temporarily frozen
 *  while being sorted).
 */

VALUE
rb_ary_frozen_p(VALUE ary)
{
    return OBJ_FROZEN(ary) ? Qtrue : Qfalse;
}

static VALUE
rb_ary_frozen_imp(VALUE ary, SEL sel)
{
    return rb_ary_frozen_p(ary);
}

void rb_ary_insert(VALUE ary, long idx, VALUE val);

static inline VALUE
ary_alloc(VALUE klass)
{
    if ((klass == 0 || klass == rb_cRubyArray || klass == rb_cNSMutableArray)
	    && rb_cRubyArray != 0) {
	NEWOBJ(ary, rb_ary_t);
	ary->basic.flags = 0;
	ary->basic.klass = rb_cRubyArray;
        ary->beg = ary->len = ary->cap = 0;
	ary->elements = NULL;
	return (VALUE)ary;
    }
    else {
	CFMutableArrayRef ary = CFArrayCreateMutable(NULL, 0,
		&kCFTypeArrayCallBacks);
	if (klass != 0 && klass != rb_cNSArray && klass != rb_cNSMutableArray) {
	    *(Class *)ary = (Class)klass;
	}
	CFMakeCollectable(ary);
	return (VALUE)ary;
    }
}

VALUE
rb_ary_new_fast(int argc, ...) 
{
    VALUE ary = ary_alloc(0);

    if (argc > 0) {
	va_list ar;

	rary_reserve(RARY(ary), argc);
	va_start(ar, argc);
	for (int i = 0; i < argc; i++) {
	    VALUE item = va_arg(ar, VALUE);
	    rary_append(RARY(ary), item);
	}
	va_end(ar);
    }

    return ary;
}

static inline void
assert_ary_len(const long len)
{
    if (len < 0) {
	rb_raise(rb_eArgError, "negative array size (or size too big)");
    }
    if ((unsigned long)len > (LONG_MAX / sizeof(VALUE))) {
	rb_raise(rb_eArgError, "array size too big");
    }
}

static VALUE
ary_new(VALUE klass, long len)
{
    assert_ary_len(len);

    VALUE ary = ary_alloc(klass);
    if (IS_RARY(ary)) {
	rary_reserve(RARY(ary), len);
    }
    return ary;
}

VALUE
rb_ary_new2(long len)
{
    return ary_new(0, len);
}

VALUE
rb_ary_new(void)
{
    return rb_ary_new2(ARY_DEFAULT_SIZE);
}

#include <stdarg.h>

VALUE
rb_ary_new3(long n, ...)
{
    VALUE ary = rb_ary_new2(n);

    va_list ar;
    va_start(ar, n);
    for (long i = 0; i < n; i++) {
	rb_ary_insert(ary, i, va_arg(ar, VALUE));
    }
    va_end(ar);

    return ary;
}

VALUE
rb_ary_new4(long n, const VALUE *elts)
{
    VALUE ary;

    ary = rb_ary_new2(n);
    if (n > 0 && elts != NULL) {
	if (IS_RARY(ary)) {
	    for (long i = 0; i < n; i++) {
		rary_append(RARY(ary), elts[i]);
	    }
	}
	else {
	    void **vals = (void **)alloca(n * sizeof(void *));

	    for (long i = 0; i < n; i++) {
		vals[i] = RB2OC(elts[i]);
	    }
	    CFArrayReplaceValues((CFMutableArrayRef)ary, CFRangeMake(0, 0),
		    (const void **)vals, n);
	}
    }

    return ary;
}

VALUE
rb_assoc_new(VALUE car, VALUE cdr)
{
    return rb_ary_new3(2, car, cdr);
}

static VALUE
to_ary(VALUE ary)
{
    return rb_convert_type(ary, T_ARRAY, "Array", "to_ary");
}

VALUE
rb_check_array_type(VALUE ary)
{
    return rb_check_convert_type(ary, T_ARRAY, "Array", "to_ary");
}

long
rb_ary_len(VALUE ary)
{
    if (IS_RARY(ary)) {
	return RARY(ary)->len;
    }
    else {
	return CFArrayGetCount((CFArrayRef)ary); 
    }
}

VALUE
rb_ary_elt(VALUE ary, long offset)
{
    if (IS_RARY(ary)) {
	if (offset < RARY(ary)->len) {
	    return rary_elt(RARY(ary), offset);
	}
    }
    else {
	if (offset < CFArrayGetCount((CFArrayRef)ary)) {
	    return OC2RB(CFArrayGetValueAtIndex((CFArrayRef)ary, offset));
	}
    }
    return Qnil;
}

VALUE
rb_ary_erase(VALUE ary, long offset)
{
    if (IS_RARY(ary)) {
	return rary_erase(RARY(ary), offset, 1);
    }
    else {
	VALUE item = OC2RB(CFArrayGetValueAtIndex((CFArrayRef)ary, offset));
	CFArrayRemoveValueAtIndex((CFMutableArrayRef)ary, offset);
	return item;
    }
}

VALUE
rb_ary_push(VALUE ary, VALUE item) 
{
    rb_ary_modify(ary);
    if (IS_RARY(ary)) {
	rary_append(RARY(ary), item);
    }
    else {
	CFArrayAppendValue((CFMutableArrayRef)ary, (const void *)RB2OC(item));
    }
    return ary;
}

inline static void
rb_ary_append(VALUE ary, int argc, VALUE *argv)
{
    rb_ary_modify(ary);
    if (IS_RARY(ary)) {
	rary_reserve(RARY(ary), argc);
	for (int i = 0; i < argc; i++) {
	    rary_append(RARY(ary), argv[i]);
	}
    }
    else {
	for (int i = 0; i < argc; i++) {
	    CFArrayAppendValue((CFMutableArrayRef)ary,
		    (const void *)RB2OC(argv[i]));
	}
    }
}

static inline void
rb_ary_resize(VALUE ary, long new_len)
{
    if (IS_RARY(ary)) {
	rary_resize(RARY(ary), new_len);
    }
    else {
	// TODO
	abort();	
    }
}

/*
 *  call-seq:
 *     Array.try_convert(obj) -> array or nil
 *
 *  Try to convert <i>obj</i> into an array, using to_ary method.
 *  Returns converted array or nil if <i>obj</i> cannot be converted
 *  for any reason.  This method is to check if an argument is an
 *  array.  
 *
 *     Array.try_convert([1])   # => [1]
 *     Array.try_convert("1")   # => nil
 *     
 *     if tmp = Array.try_convert(arg)
 *       # the argument is an array
 *     elsif tmp = String.try_convert(arg)
 *       # the argument is a string
 *     end
 *
 */

static VALUE
rb_ary_s_try_convert(VALUE dummy, SEL sel, VALUE ary)
{
    return rb_check_array_type(ary);
}

/*
 *  call-seq:
 *     Array.new(size=0, obj=nil)
 *     Array.new(array)
 *     Array.new(size) {|index| block }
 *
 *  Returns a new array. In the first form, the new array is
 *  empty. In the second it is created with _size_ copies of _obj_
 *  (that is, _size_ references to the same
 *  _obj_). The third form creates a copy of the array
 *  passed as a parameter (the array is generated by calling
 *  to_ary  on the parameter). In the last form, an array
 *  of the given size is created. Each element in this array is
 *  calculated by passing the element's index to the given block and
 *  storing the return value.
 *
 *     Array.new
 *     Array.new(2)
 *     Array.new(5, "A")
 * 
 *     # only one copy of the object is created
 *     a = Array.new(2, Hash.new)
 *     a[0]['cat'] = 'feline'
 *     a
 *     a[1]['cat'] = 'Felix'
 *     a
 * 
 *     # here multiple copies are created
 *     a = Array.new(2) { Hash.new }
 *     a[0]['cat'] = 'feline'
 *     a
 * 
 *     squares = Array.new(5) {|i| i*i}
 *     squares
 * 
 *     copy = Array.new(squares)
 */

static VALUE
rb_ary_initialize(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    ary = (VALUE)objc_msgSend((id)ary, selInit);

    if (argc ==  0) {
	if (rb_block_given_p()) {
	    rb_warning("given block not used");
	}
	rb_ary_clear(ary);
	return ary;
    }

    VALUE size, val;
    rb_scan_args(argc, argv, "02", &size, &val);
    if (argc == 1 && !FIXNUM_P(size)) {
	val = rb_check_array_type(size);
	if (!NIL_P(val)) {
	    rb_ary_replace(ary, val);
	    return ary;
	}
    }

    long len = NUM2LONG(size);
    assert_ary_len(len);

    rb_ary_modify(ary);

    if (rb_block_given_p()) {
	if (argc == 2) {
	    rb_warn("block supersedes default value argument");
	}
	rb_ary_clear(ary);
	for (long i = 0; i < len; i++) {
	    VALUE v = rb_yield(LONG2NUM(i));
	    RETURN_IF_BROKEN();
	    rb_ary_push(ary, v);
	}
    }
    else {
	rb_ary_resize(ary, len);
	for (long i = 0; i < len; i++) {
	    rb_ary_store(ary, i, val);
	}
    }
    return ary;
}

/* 
* Returns a new array populated with the given objects. 
*
*   Array.[]( 1, 'a', /^A/ )
*   Array[ 1, 'a', /^A/ ]
*   [ 1, 'a', /^A/ ]
*/

static VALUE
rb_ary_s_create(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE ary = ary_alloc(klass);

    if (argc < 0) {
	rb_raise(rb_eArgError, "negative array size");
    }

    rb_ary_append(ary, argc, argv);

    return ary;
}

void
rb_ary_insert(VALUE ary, long idx, VALUE val)
{
    if (idx < 0) {
	idx += RARRAY_LEN(ary);
	if (idx < 0) {
	    rb_raise(rb_eIndexError, "index %ld out of array",
		    idx - RARRAY_LEN(ary));
	}
    }

    if (IS_RARY(ary)) {
	if (idx > RARY(ary)->len) {
	    rary_resize(RARY(ary), idx + 1);
	    rary_store(RARY(ary), idx, val);
	}
	else {
	    rary_insert(RARY(ary), idx, val);
	}
    }
    else {
	CFArrayInsertValueAtIndex((CFMutableArrayRef)ary, idx, 
		(const void *)RB2OC(val));
    }
}

void
rb_ary_store(VALUE ary, long idx, VALUE val)
{
    if (idx < 0) {
	const long len = RARRAY_LEN(ary);
	idx += len;
	if (idx < 0) {
	    rb_raise(rb_eIndexError, "index %ld out of array",
		    idx - len);
	}
    }
    if (IS_RARY(ary)) {
	rary_store(RARY(ary), idx, val);
    }
    else {
	CFArraySetValueAtIndex((CFMutableArrayRef)ary, idx,
		(const void *)RB2OC(val));
    }
}

static VALUE
ary_shared_first(int argc, VALUE *argv, VALUE ary, bool last, bool remove)
{
    VALUE nv;
    rb_scan_args(argc, argv, "1", &nv);
    long n = NUM2LONG(nv);

    const long ary_len = RARRAY_LEN(ary);
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
    VALUE result = rb_ary_new();

    for (long i = 0; i < n; i++) {
	VALUE item = rb_ary_elt(ary, i + offset);
	rary_append(RARY(result), item);
    }

    if (remove) {
	for (long i = 0; i < n; i++) {
	    rb_ary_erase(ary, offset);
	}
    }

    return result;
}

/*
 *  call-seq:
 *     array << obj            -> array
 *  
 *  Append---Pushes the given object on to the end of this array. This
 *  expression returns the array itself, so several appends
 *  may be chained together.
 *
 *     [ 1, 2 ] << "c" << "d" << [ 3, 4 ]
 *             #=>  [ 1, 2, "c", "d", [ 3, 4 ] ]
 *
 */

static VALUE
rb_ary_push_imp(VALUE ary, SEL sel, VALUE item)
{
    return rb_ary_push(ary, item);
}

/* 
 *  call-seq:
 *     array.push(obj, ... )   -> array
 *  
 *  Append---Pushes the given object(s) on to the end of this array. This
 *  expression returns the array itself, so several appends
 *  may be chained together.
 *
 *     a = [ "a", "b", "c" ]
 *     a.push("d", "e", "f")  
 *             #=> ["a", "b", "c", "d", "e", "f"]
 */

static VALUE
rb_ary_push_m(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {
	// Even if there is nothing to push, we still need to check if the
	// receiver can be modified, to conform to RubySpec.
	rb_ary_modify(ary);
    }
    else {
	while (argc--) {
	    rb_ary_push(ary, *argv++);
	}
    }
    return ary;
}

VALUE
rb_ary_pop(VALUE ary)
{
    rb_ary_modify(ary);
    const long n = RARRAY_LEN(ary);
    if (n == 0) {
	return Qnil;
    }
    return rb_ary_erase(ary, n - 1);
}

/*
 *  call-seq:
 *     array.pop    -> obj or nil
 *     array.pop(n) -> array
 *  
 *  Removes the last element from <i>self</i> and returns it, or
 *  <code>nil</code> if the array is empty.
 *     
 *  If a number _n_ is given, returns an array of the last n elements
 *  (or less) just like <code>array.slice!(-n, n)</code> does.
 *     
 *     a = [ "a", "b", "c", "d" ]
 *     a.pop     #=> "d"
 *     a.pop(2)  #=> ["b", "c"]
 *     a         #=> ["a"]
 */

static VALUE
rb_ary_pop_m(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {
	return rb_ary_pop(ary);
    }

    rb_ary_modify(ary);
    return ary_shared_first(argc, argv, ary, true, true);
}

VALUE
rb_ary_shift(VALUE ary)
{
    rb_ary_modify(ary);
    if (RARRAY_LEN(ary) == 0) {
	return Qnil;
    }

    return rb_ary_erase(ary, 0);
}

/*
 *  call-seq:
 *     array.shift    -> obj or nil
 *     array.shift(n) -> array
 *  
 *  Returns the first element of <i>self</i> and removes it (shifting all
 *  other elements down by one). Returns <code>nil</code> if the array
 *  is empty.
 *     
 *  If a number _n_ is given, returns an array of the first n elements
 *  (or less) just like <code>array.slice!(0, n)</code> does.
 *     
 *     args = [ "-m", "-q", "filename" ]
 *     args.shift     #=> "-m"
 *     args           #=> ["-q", "filename"]
 *
 *     args = [ "-m", "-q", "filename" ]
 *     args.shift(2)  #=> ["-m", "-q"]
 *     args           #=> ["filename"]
 */

static VALUE
rb_ary_shift_m(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {
	return rb_ary_shift(ary);
    }

    rb_ary_modify(ary);
    return ary_shared_first(argc, argv, ary, false, true);
}

/*
 *  call-seq:
 *     array.unshift(obj, ...)  -> array
 *  
 *  Prepends objects to the front of <i>array</i>.
 *  other elements up one.
 *     
 *     a = [ "b", "c", "d" ]
 *     a.unshift("a")   #=> ["a", "b", "c", "d"]
 *     a.unshift(1, 2)  #=> [ 1, 2, "a", "b", "c", "d"]
 */

static VALUE
rb_ary_unshift_m(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    rb_ary_modify(ary);

    for (int i = argc - 1; i >= 0; i--) {
	rb_ary_insert(ary, 0, argv[i]);
    }

    return ary;
}

VALUE
rb_ary_unshift(VALUE ary, VALUE item)
{
    return rb_ary_unshift_m(ary, 0, 1, &item);
}

static void *rb_objc_ary_cptr_assoc_key = NULL;

const VALUE *
rb_ary_ptr(VALUE ary)
{
    if (IS_RARY(ary)) {
	return rary_ptr(RARY(ary));
    }

    const long len = RARRAY_LEN(ary);
    if (len == 0) {
	return NULL;
    }

    VALUE *values = (VALUE *)xmalloc(sizeof(VALUE) * len);
    CFArrayGetValues((CFArrayRef)ary, CFRangeMake(0, len), 
	    (const void **)values);

    for (long i = 0; i < len; i++) {
	values[i] = OC2RB(values[i]);
    }
    rb_objc_set_associative_ref((void *)ary, &rb_objc_ary_cptr_assoc_key,
	    values);

    return values;
}

VALUE
rb_ary_entry(VALUE ary, long offset)
{
    const long n = RARRAY_LEN(ary);
    if (n == 0) {
	return Qnil;
    }
    if (offset < 0) {
	offset += n;
    }
    if (offset < 0 || n <= offset) {
	return Qnil;
    }
    return rb_ary_elt(ary, offset);
}

VALUE
rb_ary_subseq(VALUE ary, long beg, long len)
{
    if (beg < 0 || len < 0) {
	return Qnil;
    }

    const long n = RARRAY_LEN(ary);
    if (beg > n) {
	return Qnil;
    }

    if (n < len || n < beg + len) {
	len = n - beg;
    }

    VALUE newary = ary_alloc(rb_obj_class(ary));
    if (len > 0) {
	if (IS_RARY(newary)) {
	    if (IS_RARY(ary)) {
		rary_concat(RARY(newary), RARY(ary), beg, len);
	    }
	    else {
		rary_reserve(RARY(newary), len);
		for (long i = 0; i < len; i++) {
		    VALUE item = rb_ary_elt(ary, beg + i);
		    rary_append(RARY(newary), item);
		}
	    }	
	}
	else {
	    void **values = (void **)alloca(sizeof(void *) * len);
	    CFArrayGetValues((CFArrayRef)ary, CFRangeMake(beg, len),
		    (const void **)values);
	    CFArrayReplaceValues((CFMutableArrayRef)newary, CFRangeMake(0, 0), 
		    (const void **)values, len);
	}
    }	
    return newary;
}

/* 
 *  call-seq:
 *     array[index]                -> obj      or nil
 *     array[start, length]        -> an_array or nil
 *     array[range]                -> an_array or nil
 *     array.slice(index)          -> obj      or nil
 *     array.slice(start, length)  -> an_array or nil
 *     array.slice(range)          -> an_array or nil
 *
 *  Element Reference---Returns the element at _index_,
 *  or returns a subarray starting at _start_ and
 *  continuing for _length_ elements, or returns a subarray
 *  specified by _range_.
 *  Negative indices count backward from the end of the
 *  array (-1 is the last element). Returns nil if the index
 *  (or starting index) are out of range.
 *
 *     a = [ "a", "b", "c", "d", "e" ]
 *     a[2] +  a[0] + a[1]    #=> "cab"
 *     a[6]                   #=> nil
 *     a[1, 2]                #=> [ "b", "c" ]
 *     a[1..3]                #=> [ "b", "c", "d" ]
 *     a[4..7]                #=> [ "e" ]
 *     a[6..10]               #=> nil
 *     a[-3, 3]               #=> [ "c", "d", "e" ]
 *     # special cases
 *     a[5]                   #=> nil
 *     a[5, 1]                #=> []
 *     a[5..10]               #=> []
 *
 */

VALUE
rb_ary_aref(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    long beg, len;

    if (argc == 2) {
	beg = NUM2LONG(argv[0]);
	len = NUM2LONG(argv[1]);
	if (beg < 0) {
	    beg += RARRAY_LEN(ary);
	}
	return rb_ary_subseq(ary, beg, len);
    }
    if (argc != 1) {
	rb_scan_args(argc, argv, "11", 0, 0);
    }
    VALUE arg = argv[0];
    /* special case - speeding up */
    if (FIXNUM_P(arg)) {
	return rb_ary_entry(ary, FIX2LONG(arg));
    }
    /* check if idx is Range */
    switch (rb_range_beg_len(arg, &beg, &len, RARRAY_LEN(ary), 0)) {
	case Qfalse:
	    break;
	case Qnil:
	    return Qnil;
	default:
	    return rb_ary_subseq(ary, beg, len);
    }
    return rb_ary_entry(ary, NUM2LONG(arg));
}

/* 
 *  call-seq:
 *     array.at(index)   ->   obj  or nil
 *
 *  Returns the element at _index_. A
 *  negative index counts from the end of _self_.  Returns +nil+
 *  if the index is out of range. See also <code>Array#[]</code>.
 *
 *     a = [ "a", "b", "c", "d", "e" ]
 *     a.at(0)     #=> "a"
 *     a.at(-1)    #=> "e"
 */

static VALUE
rb_ary_at(VALUE ary, SEL sel, VALUE pos)
{
    return rb_ary_entry(ary, NUM2LONG(pos));
}

/*
 *  call-seq:
 *     array.first     ->   obj or nil
 *     array.first(n)  ->   an_array
 *  
 *  Returns the first element, or the first +n+ elements, of the array.
 *  If the array is empty, the first form returns <code>nil</code>, and the
 *  second form returns an empty array.
 *     
 *     a = [ "q", "r", "s", "t" ]
 *     a.first     #=> "q"
 *     a.first(2)  #=> ["q", "r"]
 */

static VALUE
rb_ary_first(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {
	if (RARRAY_LEN(ary) == 0) {
	    return Qnil;
	}
	return RARRAY_AT(ary, 0);
    }
    else {
	return ary_shared_first(argc, argv, ary, false, false);
    }
}

/*
 *  call-seq:
 *     array.last     ->  obj or nil
 *     array.last(n)  ->  an_array
 *  
 *  Returns the last element(s) of <i>self</i>. If the array is empty,
 *  the first form returns <code>nil</code>.
 *     
 *     a = [ "w", "x", "y", "z" ]
 *     a.last     #=> "z"
 *     a.last(2)  #=> ["y", "z"]
 */

VALUE
rb_ary_last(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {
	const long n = RARRAY_LEN(ary);
	if (n == 0) {
	    return Qnil;
	}
	return RARRAY_AT(ary, n - 1);
    }
    else {
	return ary_shared_first(argc, argv, ary, true, false);
    }
}

/*
 *  call-seq:
 *     array.fetch(index)                    -> obj
 *     array.fetch(index, default )          -> obj
 *     array.fetch(index) {|index| block }   -> obj
 *  
 *  Tries to return the element at position <i>index</i>. If the index
 *  lies outside the array, the first form throws an
 *  <code>IndexError</code> exception, the second form returns
 *  <i>default</i>, and the third form returns the value of invoking
 *  the block, passing in the index. Negative values of <i>index</i>
 *  count from the end of the array.
 *     
 *     a = [ 11, 22, 33, 44 ]
 *     a.fetch(1)               #=> 22
 *     a.fetch(-1)              #=> 44
 *     a.fetch(4, 'cat')        #=> "cat"
 *     a.fetch(4) { |i| i*i }   #=> 16
 */

static VALUE
rb_ary_fetch(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE pos, ifnone;

    rb_scan_args(argc, argv, "11", &pos, &ifnone);
    const bool block_given = rb_block_given_p();
    if (block_given && argc == 2) {
	rb_warn("block supersedes default value argument");
    }

    long idx = NUM2LONG(pos);
    if (idx < 0) {
	idx +=  RARRAY_LEN(ary);
    }
    if (idx < 0 || RARRAY_LEN(ary) <= idx) {
	if (block_given) {
	    return rb_yield(pos);
	}
	if (argc == 1) {
	    rb_raise(rb_eIndexError, "index %ld out of array", idx);
	}
	return ifnone;
    }
    return RARRAY_AT(ary, idx);
}

/*
 *  call-seq:
 *     array.index(obj)           ->  int or nil
 *     array.index {|item| block} ->  int or nil
 *  
 *  Returns the index of the first object in <i>self</i> such that is
 *  <code>==</code> to <i>obj</i>. If a block is given instead of an
 *  argument, returns first object for which <em>block</em> is true.
 *  Returns <code>nil</code> if no match is found.
 *     
 *     a = [ "a", "b", "c" ]
 *     a.index("b")        #=> 1
 *     a.index("z")        #=> nil
 *     a.index{|x|x=="b"}  #=> 1
 *
 *  This is an alias of <code>#find_index</code>.
 */

static VALUE
rb_ary_index(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE val;

    const long n = RARRAY_LEN(ary);
    if (rb_scan_args(argc, argv, "01", &val) == 0) {
	RETURN_ENUMERATOR(ary, 0, 0);
	for (long i = 0; i < n; i++) {
	    VALUE v = rb_yield(RARRAY_AT(ary, i));
	    RETURN_IF_BROKEN();
	    if (RTEST(v)) {
		return LONG2NUM(i);
	    }
	}
    }
    else if (n > 0) {
	if (IS_RARY(ary)) {
	    size_t pos = rary_index_of_item(RARY(ary), 0, val);
	    if (pos != NOT_FOUND) {
		return LONG2NUM(pos);
	    }
	}
	else {
	    CFIndex idx = CFArrayGetFirstIndexOfValue((CFArrayRef)ary,
		    CFRangeMake(0, n), (const void *)RB2OC(val));
	    if (idx != -1) {
		return LONG2NUM(idx);
	    }
	}
    }
    return Qnil;
}

/*
 *  call-seq:
 *     array.rindex(obj)    ->  int or nil
 *  
 *  Returns the index of the last object in <i>array</i>
 *  <code>==</code> to <i>obj</i>. If a block is given instead of an
 *  argument, returns first object for which <em>block</em> is
 *  true. Returns <code>nil</code> if no match is found.
 *     
 *     a = [ "a", "b", "b", "b", "c" ]
 *     a.rindex("b")        #=> 3
 *     a.rindex("z")        #=> nil
 *     a.rindex{|x|x=="b"}  #=> 3
 */

static VALUE
rb_ary_rindex(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    const long n = RARRAY_LEN(ary);
    long i = n;

    if (argc == 0) {
	RETURN_ENUMERATOR(ary, 0, 0);
	while (i--) {
	    VALUE v = rb_yield(RARRAY_AT(ary, i));
	    RETURN_IF_BROKEN();
	    if (RTEST(v)) {
		return LONG2NUM(i);
	    }
	    if (i > n) {
		i = n;
	    }
	}
    }
    else {
	VALUE val;
 	rb_scan_args(argc, argv, "01", &val);
	
	// TODO: optimize for RARY
	i = CFArrayGetLastIndexOfValue((CFArrayRef)ary, CFRangeMake(0, n),
	   (const void *)RB2OC(val));
	if (i != -1) {
	    return LONG2NUM(i);
	}
    }
    return Qnil;
}

VALUE
rb_ary_to_ary(VALUE obj)
{
    if (TYPE(obj) == T_ARRAY) {
	return obj;
    }
    if (rb_respond_to(obj, rb_intern("to_ary"))) {
	return to_ary(obj);
    }
    return rb_ary_new3(1, obj);
}

static void
rb_ary_splice(VALUE ary, long beg, long len, VALUE rpl)
{
    const long n = RARRAY_LEN(ary);
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

    long rlen;
    if (rpl == Qundef) {
	rlen = 0;
    }
    else {
	rpl = rb_ary_to_ary(rpl);
	rlen = RARRAY_LEN(rpl);
    }

    rb_ary_modify(ary);
    if (IS_RARY(ary) && (rpl == Qundef || IS_RARY(rpl))) {
	if (ary == rpl) {
	    rpl = rb_ary_dup(rpl);
	}
	if (beg >= n) {
	    for (long i = n; i < beg; i++) {
		rary_append(RARY(ary), Qnil);
	    }
	    if (rlen > 0) {
		rary_concat(RARY(ary), RARY(rpl), 0, rlen);
	    }
	}
	else if (len == rlen) {
	    for (long i = 0; i < len; i++) {
		rary_replace(RARY(ary), beg + i, rary_elt(RARY(rpl), i));
	    }	
	}
	else {
	    rary_erase(RARY(ary), beg, len);
	    for (long i = 0; i < rlen; i++) {
		rary_insert(RARY(ary), beg + i, rary_elt(RARY(rpl), i));
	    }
	}
    }
    else {
	if (beg >= n) {
	    for (long i = n; i < beg; i++) {
		CFArrayAppendValue((CFMutableArrayRef)ary,
			(const void *)kCFNull);
	    }
	    if (rlen > 0)  {
		CFArrayAppendArray((CFMutableArrayRef)ary, (CFArrayRef)rpl,
			CFRangeMake(0, rlen));
	    }
	}
	else {
	    void **values;
	    if (rlen > 0) {
		values = (void **)alloca(sizeof(void *) * rlen);
		CFArrayGetValues((CFArrayRef)rpl, CFRangeMake(0, rlen),
			(const void **)values);
	    }
	    else {
		values = NULL;
	    }
	    CFArrayReplaceValues((CFMutableArrayRef)ary,
		    CFRangeMake(beg, len),
		    (const void **)values,
		    rlen);
	}
    }
}

/* 
 *  call-seq:
 *     array[index]         = obj                     ->  obj
 *     array[start, length] = obj or an_array or nil  ->  obj or an_array or nil
 *     array[range]         = obj or an_array or nil  ->  obj or an_array or nil
 *
 *  Element Assignment---Sets the element at _index_,
 *  or replaces a subarray starting at _start_ and
 *  continuing for _length_ elements, or replaces a subarray
 *  specified by _range_.  If indices are greater than
 *  the current capacity of the array, the array grows
 *  automatically. A negative indices will count backward
 *  from the end of the array. Inserts elements if _length_ is
 *  zero. An +IndexError+ is raised if a negative index points
 *  past the beginning of the array. See also
 *  <code>Array#push</code>, and <code>Array#unshift</code>.
 * 
 *     a = Array.new
 *     a[4] = "4";                 #=> [nil, nil, nil, nil, "4"]
 *     a[0, 3] = [ 'a', 'b', 'c' ] #=> ["a", "b", "c", nil, "4"]
 *     a[1..2] = [ 1, 2 ]          #=> ["a", 1, 2, nil, "4"]
 *     a[0, 2] = "?"               #=> ["?", 2, nil, "4"]
 *     a[0..2] = "A"               #=> ["A", "4"]
 *     a[-1]   = "Z"               #=> ["A", "Z"]
 *     a[1..-1] = nil              #=> ["A", nil]
 *     a[1..-1] = []               #=> ["A"]
 */

static VALUE
rb_ary_aset(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    long offset, beg, len;

    if (argc == 3) {
	rb_ary_splice(ary, NUM2LONG(argv[0]), NUM2LONG(argv[1]), argv[2]);
	return argv[2];
    }
    if (argc != 2) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 2)", argc);
    }
    if (FIXNUM_P(argv[0])) {
	offset = FIX2LONG(argv[0]);
	goto fixnum;
    }
    if (rb_range_beg_len(argv[0], &beg, &len, RARRAY_LEN(ary), 1)) {
	/* check if idx is Range */
	rb_ary_splice(ary, beg, len, argv[1]);
	return argv[1];
    }

    offset = NUM2LONG(argv[0]);
fixnum:
    rb_ary_store(ary, offset, argv[1]);
    return argv[1];
}

/*
 *  call-seq:
 *     array.insert(index, obj...)  -> array
 *  
 *  Inserts the given values before the element with the given index
 *  (which may be negative).
 *     
 *     a = %w{ a b c d }
 *     a.insert(2, 99)         #=> ["a", "b", 99, "c", "d"]
 *     a.insert(-2, 1, 2, 3)   #=> ["a", "b", 99, "c", 1, 2, 3, "d"]
 */

static VALUE
rb_ary_insert_m(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    if (argc == 1) {
	rb_ary_modify(ary);
	return ary;
    }
    if (argc < 1) {
	rb_raise(rb_eArgError, "wrong number of arguments (at least 1)");
    }
    long pos = NUM2LONG(argv[0]);
    if (pos == -1) {
	pos = RARRAY_LEN(ary);
    }
    if (pos < 0) {
	pos++;
    }
    rb_ary_modify(ary);
    if (argc == 2) {
	rb_ary_insert(ary, pos, argv[1]);
    }
    else {
	rb_ary_splice(ary, pos, 0, rb_ary_new4(argc - 1, argv + 1));
    }
    return ary;
}

/*
 *  call-seq:
 *     array.each {|item| block }   ->   array
 *  
 *  Calls <i>block</i> once for each element in <i>self</i>, passing that
 *  element as a parameter.
 *     
 *     a = [ "a", "b", "c" ]
 *     a.each {|x| print x, " -- " }
 *     
 *  produces:
 *     
 *     a -- b -- c --
 */

VALUE
rb_ary_each(VALUE ary, SEL sel)
{
    long i;

    RETURN_ENUMERATOR(ary, 0, 0);
    for (i = 0; i < RARRAY_LEN(ary); i++) {
	rb_yield(RARRAY_AT(ary, i));
	RETURN_IF_BROKEN();
    }
    return ary;
}

/*
 *  call-seq:
 *     array.each_index {|index| block }  ->  array
 *  
 *  Same as <code>Array#each</code>, but passes the index of the element
 *  instead of the element itself.
 *     
 *     a = [ "a", "b", "c" ]
 *     a.each_index {|x| print x, " -- " }
 *     
 *  produces:
 *     
 *     0 -- 1 -- 2 --
 */

static VALUE
rb_ary_each_index(VALUE ary, SEL sel)
{
    long i, n;
    RETURN_ENUMERATOR(ary, 0, 0);

    for (i = 0, n = RARRAY_LEN(ary); i < n; i++) {
	rb_yield(LONG2NUM(i));
	RETURN_IF_BROKEN();
    }
    return ary;
}

/*
 *  call-seq:
 *     array.reverse_each {|item| block } 
 *  
 *  Same as <code>Array#each</code>, but traverses <i>self</i> in reverse
 *  order.
 *     
 *     a = [ "a", "b", "c" ]
 *     a.reverse_each {|x| print x, " " }
 *     
 *  produces:
 *     
 *     c b a
 */

static VALUE
rb_ary_reverse_each(VALUE ary, SEL sel)
{
    long n, len;

    RETURN_ENUMERATOR(ary, 0, 0);
    len = RARRAY_LEN(ary);
    while (len--) {
	rb_yield(RARRAY_AT(ary, len));
	RETURN_IF_BROKEN();
	n = RARRAY_LEN(ary);
	if (n < len) {
	    len = n;
	}
    }
    return ary;
}

/*
 *  call-seq:
 *     array.length -> int
 *  
 *  Returns the number of elements in <i>self</i>. May be zero.
 *     
 *     [ 1, 2, 3, 4, 5 ].length   #=> 5
 */

static VALUE
rb_ary_length(VALUE ary, SEL sel)
{
    return LONG2NUM(RARRAY_LEN(ary));
}

/*
 *  call-seq:
 *     array.empty?   -> true or false
 *  
 *  Returns <code>true</code> if <i>self</i> array contains no elements.
 *     
 *     [].empty?   #=> true
 */

static VALUE
rb_ary_empty_p(VALUE ary, SEL sel)
{
    return RARRAY_LEN(ary) == 0 ? Qtrue : Qfalse;
}

VALUE
rb_ary_dup(VALUE ary)
{
    VALUE dup;

    if (rb_obj_is_kind_of(ary, rb_cRubyArray)) {
	dup = rb_ary_new();
	rary_concat(RARY(dup), RARY(ary), 0, RARY(ary)->len);
    }
    else {
	dup = (VALUE)CFArrayCreateMutableCopy(NULL, 0, (CFArrayRef)ary);
	CFMakeCollectable((CFMutableArrayRef)dup);
    }

    *(VALUE *)dup = *(VALUE *)ary;

    if (OBJ_TAINTED(ary)) {
	OBJ_TAINT(dup);
    }

    if (OBJ_UNTRUSTED(ary)) {
	OBJ_UNTRUST(dup);
    }

    return dup;
}

static VALUE
rb_ary_dup_imp(VALUE ary, SEL sel)
{
    return rb_ary_dup(ary);
}

static VALUE
rb_ary_clone(VALUE ary, SEL sel)
{
    VALUE clone = rb_ary_dup(ary);
    if (OBJ_FROZEN(ary)) {
	OBJ_FREEZE(clone);
    }
    if (OBJ_UNTRUSTED(ary)) {
        OBJ_UNTRUST(clone);
    }
    return clone;
}

extern VALUE rb_output_fs;

static VALUE
recursive_join(VALUE ary, VALUE argp, int recur)
{
    VALUE *arg = (VALUE *)argp;
    if (recur) {
	return rb_usascii_str_new2("[...]");
    }
    return rb_ary_join(arg[0], arg[1]);
}

VALUE
rb_exec_recursive(VALUE (*func) (VALUE, VALUE, int), VALUE obj, VALUE arg)
{
    // TODO check!
    return (*func) (obj, arg, Qfalse);
}

VALUE
rb_ary_join(VALUE ary, VALUE sep)
{
    long i, count;
    int taint = Qfalse;
    int untrust = Qfalse;
    VALUE result, tmp;

    if (RARRAY_LEN(ary) == 0) {
	return rb_str_new(0, 0);
    }
    if (OBJ_TAINTED(ary) || OBJ_TAINTED(sep)) {
	taint = Qtrue;
    }
    if (OBJ_UNTRUSTED(ary) || OBJ_UNTRUSTED(sep)) {
        untrust = Qtrue;
    }
    result = rb_str_new(0, 0);

    for (i = 0, count = RARRAY_LEN(ary); i < count; i++) {
	tmp = RARRAY_AT(ary, i);
	switch (TYPE(tmp)) {
	    case T_STRING:
		break;
	    case T_ARRAY:
		{
		    VALUE args[2];

		    args[0] = tmp;
		    args[1] = sep;
		    tmp = rb_exec_recursive(recursive_join, ary, (VALUE)args);
		}
		break;
	    default:
		tmp = rb_obj_as_string(tmp);
	}
	if (i > 0 && !NIL_P(sep)) {
	    rb_str_buf_append(result, sep);
	}
	rb_str_buf_append(result, tmp);
	if (OBJ_TAINTED(tmp)) {
	    taint = Qtrue;
	}
	if (OBJ_UNTRUSTED(tmp)) {
        untrust = Qtrue;
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

/*
 *  call-seq:
 *     array.join(sep=$,)    -> str
 *  
 *  Returns a string created by converting each element of the array to
 *  a string, separated by <i>sep</i>.
 *     
 *     [ "a", "b", "c" ].join        #=> "abc"
 *     [ "a", "b", "c" ].join("-")   #=> "a-b-c"
 */

static VALUE
rb_ary_join_m(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE sep;

    rb_scan_args(argc, argv, "01", &sep);
    if (NIL_P(sep)) {
	sep = rb_output_fs;
    }
    
    return rb_ary_join(ary, sep);
}

static VALUE
inspect_ary(VALUE ary, VALUE dummy, int recur)
{
    if (recur) {
	return rb_tainted_str_new2("[...]");
    }

    bool tainted = OBJ_TAINTED(ary);
    VALUE str = rb_str_buf_new2("[");
    for (long i = 0; i < RARRAY_LEN(ary); i++) {
	VALUE s = rb_inspect(RARRAY_AT(ary, i));
	if (OBJ_TAINTED(s)) {
	    tainted = true;
	}
	if (i > 0) {
	    rb_str_buf_cat2(str, ", ");
	}
	rb_str_buf_append(str, s);
    }
    rb_str_buf_cat2(str, "]");

    if (tainted) {
	OBJ_TAINT(str);
    }
    return str;
}

/*
 *  call-seq:
 *     array.to_s -> string
 *     array.inspect  -> string
 *
 *  Create a printable version of <i>array</i>.
 */

static VALUE
rb_ary_inspect(VALUE ary, SEL sel)
{
    if (RARRAY_LEN(ary) == 0) {
	return rb_usascii_str_new2("[]");
    }
    return rb_exec_recursive(inspect_ary, ary, 0);
}

VALUE
rb_ary_to_s(VALUE ary)
{
    return rb_ary_inspect(ary, 0);
}

/*
 *  call-seq:
 *     array.to_a     -> array
 *  
 *  Returns _self_. If called on a subclass of Array, converts
 *  the receiver to an Array object.
 */

static VALUE
rb_ary_to_a(VALUE ary, SEL sel)
{
    if (!rb_objc_ary_is_pure(ary)) {
	VALUE dup = rb_ary_new2(RARRAY_LEN(ary));
	rb_ary_replace(dup, ary);
	return dup;
    }
    return ary;
}

/*
 *  call-seq:
 *     array.to_ary -> array
 *  
 *  Returns _self_.
 */

static VALUE
rb_ary_to_ary_m(VALUE ary, SEL sel)
{
    return ary;
}

VALUE
rb_ary_reverse(VALUE ary)
{
    rb_ary_modify(ary);
    if (IS_RARY(ary)) {
	rary_reverse(RARY(ary));
    }
    else {
	const long n = RARRAY_LEN(ary);
	if (n > 0) {
	    void **values = (void **)alloca(sizeof(void *) * n);
	    CFRange range = CFRangeMake(0, n);
	    CFArrayGetValues((CFArrayRef)ary, range, (const void **)values);

	    long i;
	    for (i = 0; i < (n / 2); i++) {
		void *v = values[i];
		values[i] = values[n - i - 1];
		values[n - i - 1] = v;
	    }
	    CFArrayReplaceValues((CFMutableArrayRef)ary, range,
		    (const void **)values, n);
	}
    }
    return ary;
}

/*
 *  call-seq:
 *     array.reverse!   -> array 
 *  
 *  Reverses _self_ in place.
 *     
 *     a = [ "a", "b", "c" ]
 *     a.reverse!       #=> ["c", "b", "a"]
 *     a                #=> ["c", "b", "a"]
 */

static VALUE
rb_ary_reverse_bang(VALUE ary, SEL sel)
{
    return rb_ary_reverse(ary);
}

/*
 *  call-seq:
 *     array.reverse -> an_array
 *  
 *  Returns a new array containing <i>self</i>'s elements in reverse order.
 *     
 *     [ "a", "b", "c" ].reverse   #=> ["c", "b", "a"]
 *     [ 1 ].reverse               #=> [1]
 */

static VALUE
rb_ary_reverse_m(VALUE ary, SEL sel)
{
    return rb_ary_reverse(rb_ary_dup(ary));
}

static int
sort_1(void *dummy, const void *ap, const void *bp)
{
    if (*(VALUE *)dummy == 0) {
	VALUE a = *(VALUE *)ap;
	VALUE b = *(VALUE *)bp;
	VALUE retval = rb_yield_values(2, a, b);

	VALUE v = rb_vm_pop_broken_value();
	if (v != Qundef) {
	    // break was performed, we marked the dummy variable with its
	    // value and we will make sure further calls are ignored.
	    *(VALUE *)dummy = v;
	    return 0;
	}
	return rb_cmpint(retval, a, b);
    }
    else {
	return 0;
    }
}

static int
sort_2(void *dummy, const void *ap, const void *bp)
{
    VALUE a = *(VALUE *)ap;
    VALUE b = *(VALUE *)bp;

    if (FIXNUM_P(a) && FIXNUM_P(b)) {
	if ((long)a > (long)b) {
	    return 1;
	}
	if ((long)a < (long)b) {
	    return -1;
	}
	return 0;
    }
    else if (FIXFLOAT_P(a) && FIXFLOAT_P(b)) {
	const double fa = FIXFLOAT2DBL(a);
	const double fb = FIXFLOAT2DBL(b);
	if (fa > fb) {
	    return 1;
	}
	if (fa < fb) {
	    return -1;
	}
	return 0;
    }

    /* FIXME optimize!!! */
    if (TYPE(a) == T_STRING) {
	if (TYPE(b) == T_STRING) {
	    return rb_str_cmp(a, b);
	}
    }

    VALUE retval = rb_objs_cmp(a, b);
    return rb_cmpint(retval, a, b);
}

static int
cf_sort_1(const void *a, const void *b, void *dummy)
{
    VALUE ra = OC2RB(a);
    VALUE rb = OC2RB(b);
    return sort_1(dummy, &ra, &rb);
}

static int
cf_sort_2(const void *a, const void *b, void *dummy)
{
    VALUE ra = OC2RB(a);
    VALUE rb = OC2RB(b);
    return sort_2(dummy, &ra, &rb);
}

/*
 *  call-seq:
 *     array.sort!                   -> array
 *     array.sort! {| a,b | block }  -> array 
 *  
 *  Sorts _self_. Comparisons for
 *  the sort will be done using the <code><=></code> operator or using
 *  an optional code block. The block implements a comparison between
 *  <i>a</i> and <i>b</i>, returning -1, 0, or +1. See also
 *  <code>Enumerable#sort_by</code>.
 *     
 *     a = [ "d", "a", "e", "c", "b" ]
 *     a.sort                    #=> ["a", "b", "c", "d", "e"]
 *     a.sort {|x,y| y <=> x }   #=> ["e", "d", "c", "b", "a"]
 */

static VALUE
rb_ary_sort_bang1(VALUE ary, bool is_dup)
{
    const long n = RARRAY_LEN(ary);
    if (n > 1) {
	if (rb_block_given_p()) {
	    VALUE tmp = is_dup ? ary : rb_ary_dup(ary);
	    VALUE break_val = 0;

	    if (IS_RARY(ary)) {
		qsort_r(rary_ptr(RARY(tmp)), n, sizeof(VALUE), &break_val,
			sort_1);
	    }
	    else {
		CFArraySortValues((CFMutableArrayRef)tmp,
			CFRangeMake(0, n),
			(CFComparatorFunction)cf_sort_1,
			&break_val);
	    }

	    if (break_val != 0) {
		return break_val;
	    }
	    if (!is_dup) {
		rb_ary_replace(ary, tmp);
	    }
	}
	else {
	    if (IS_RARY(ary)) {
		qsort_r(rary_ptr(RARY(ary)), n, sizeof(VALUE), NULL, sort_2);
	    }
	    else {
		CFArraySortValues((CFMutableArrayRef)ary,
			CFRangeMake(0, n),
			(CFComparatorFunction)cf_sort_2,
			NULL);
	    }
	}
    }
    return ary;
}

static VALUE
rb_ary_sort_bang_imp(VALUE ary, SEL sel)
{
    rb_ary_modify(ary);
    return rb_ary_sort_bang1(ary, false);
}

VALUE
rb_ary_sort_bang(VALUE ary)
{
    return rb_ary_sort_bang_imp(ary, 0);
}

/*
 *  call-seq:
 *     array.sort                   -> an_array 
 *     array.sort {| a,b | block }  -> an_array 
 *  
 *  Returns a new array created by sorting <i>self</i>. Comparisons for
 *  the sort will be done using the <code><=></code> operator or using
 *  an optional code block. The block implements a comparison between
 *  <i>a</i> and <i>b</i>, returning -1, 0, or +1. See also
 *  <code>Enumerable#sort_by</code>.
 *     
 *     a = [ "d", "a", "e", "c", "b" ]
 *     a.sort                    #=> ["a", "b", "c", "d", "e"]
 *     a.sort {|x,y| y <=> x }   #=> ["e", "d", "c", "b", "a"]
 */

static VALUE
rb_ary_sort_imp(VALUE ary, SEL sel)
{
    ary = rb_ary_dup(ary);
    return rb_ary_sort_bang1(ary, true);
}

VALUE
rb_ary_sort(VALUE ary)
{
    return rb_ary_sort_imp(ary, 0);
}


/*
 *  call-seq:
 *     array.collect {|item| block }  -> an_array
 *     array.map     {|item| block }  -> an_array
 *  
 *  Invokes <i>block</i> once for each element of <i>self</i>. Creates a 
 *  new array containing the values returned by the block.
 *  See also <code>Enumerable#collect</code>.
 *     
 *     a = [ "a", "b", "c", "d" ]
 *     a.collect {|x| x + "!" }   #=> ["a!", "b!", "c!", "d!"]
 *     a                          #=> ["a", "b", "c", "d"]
 */

static VALUE
rb_ary_collect(VALUE ary, SEL sel)
{
    long i;
    VALUE collect;

    RETURN_ENUMERATOR(ary, 0, 0);
    collect = rb_ary_new();
    rb_ary_set_capacity(collect, RARRAY_LEN(ary));
    for (i = 0; i < RARRAY_LEN(ary); i++) {
	VALUE v = rb_yield(RARRAY_AT(ary, i));
	RETURN_IF_BROKEN();
	rb_ary_push(collect, v);
    }
    return collect;
}


/* 
 *  call-seq:
 *     array.collect! {|item| block }   ->   array
 *     array.map!     {|item| block }   ->   array
 *
 *  Invokes the block once for each element of _self_, replacing the
 *  element with the value returned by _block_.
 *  See also <code>Enumerable#collect</code>.
 *   
 *     a = [ "a", "b", "c", "d" ]
 *     a.collect! {|x| x + "!" }
 *     a             #=>  [ "a!", "b!", "c!", "d!" ]
 */

static VALUE
rb_ary_collect_bang(VALUE ary, SEL sel)
{
    long i;

    RETURN_ENUMERATOR(ary, 0, 0);
    rb_ary_modify(ary);
    for (i = 0; i < RARRAY_LEN(ary); i++) {
	VALUE v = rb_yield(RARRAY_AT(ary, i));
	RETURN_IF_BROKEN();
	rb_ary_store(ary, i, v);
    }
    return ary;
}

VALUE
rb_get_values_at(VALUE obj, long olen, int argc, VALUE *argv, VALUE (*func) (VALUE, long))
{
    VALUE result = rb_ary_new2(argc);
    long beg, len, i, j;

    for (i=0; i<argc; i++) {
	if (FIXNUM_P(argv[i])) {
	    rb_ary_push(result, (*func)(obj, FIX2LONG(argv[i])));
	    continue;
	}
	/* check if idx is Range */
	switch (rb_range_beg_len(argv[i], &beg, &len, olen, 0)) {
	  case Qfalse:
	    break;
	  case Qnil:
	    continue;
	  default:
	    for (j=0; j<len; j++) {
		rb_ary_push(result, (*func)(obj, j+beg));
	    }
	    continue;
	}
	rb_ary_push(result, (*func)(obj, NUM2LONG(argv[i])));
    }
    return result;
}

/* 
 *  call-seq:
 *     array.values_at(selector,... )  -> an_array
 *
 *  Returns an array containing the elements in
 *  _self_ corresponding to the given selector(s). The selectors
 *  may be either integer indices or ranges. 
 *  See also <code>Array#select</code>.
 * 
 *     a = %w{ a b c d e f }
 *     a.values_at(1, 3, 5)
 *     a.values_at(1, 3, 5, 7)
 *     a.values_at(-1, -3, -5, -7)
 *     a.values_at(1..3, 2...5)
 */

static VALUE
rb_ary_values_at(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    return rb_get_values_at(ary, RARRAY_LEN(ary), argc, argv, rb_ary_entry);
}


/*
 *  call-seq:
 *     array.select {|item| block } -> an_array
 *  
 *  Invokes the block passing in successive elements from <i>array</i>,
 *  returning an array containing those elements for which the block
 *  returns a true value (equivalent to <code>Enumerable#select</code>).
 *     
 *     a = %w{ a b c d e f }
 *     a.select {|v| v =~ /[aeiou]/}   #=> ["a", "e"]
 */

static VALUE
rb_ary_select(VALUE ary, SEL sel)
{
    VALUE result;
    long n, i;

    RETURN_ENUMERATOR(ary, 0, 0);
    n = RARRAY_LEN(ary);
    result = rb_ary_new2(n);
    for (i = 0; i < n; i++) {
	VALUE v = RARRAY_AT(ary, i);
	VALUE v2 = rb_yield(v);
	RETURN_IF_BROKEN();
	if (RTEST(v2)) {
	    rb_ary_push(result, v);
	}
    }
    return result;
}

/*
 *  call-seq:
 *     array.delete(obj)            -> obj or nil 
 *     array.delete(obj) { block }  -> obj or nil
 *  
 *  Deletes items from <i>self</i> that are equal to <i>obj</i>. If
 *  the item is not found, returns <code>nil</code>. If the optional
 *  code block is given, returns the result of <i>block</i> if the item
 *  is not found.
 *     
 *     a = [ "a", "b", "b", "b", "c" ]
 *     a.delete("b")                   #=> "b"
 *     a                               #=> ["a", "c"]
 *     a.delete("z")                   #=> nil
 *     a.delete("z") { "not found" }   #=> "not found"
 */

static VALUE
rb_ary_delete_imp(VALUE ary, SEL sel, VALUE item)
{
    rb_ary_modify(ary);

    bool changed = false;
    if (IS_RARY(ary)) {
	size_t pos = 0;
	while (pos < RARY(ary)->len
		&& (pos = rary_index_of_item(RARY(ary), pos, item))
		!= NOT_FOUND) {
	    rary_erase(RARY(ary), pos, 1);
	    changed = true;
	}
    }
    else {
	const void *ocitem = (const void *)RB2OC(item);
	long n = RARRAY_LEN(ary);
	CFRange r = CFRangeMake(0, n);
	long i = 0;
	while ((i = CFArrayGetFirstIndexOfValue((CFArrayRef)ary, r, ocitem))
		!= -1) {
	    CFArrayRemoveValueAtIndex((CFMutableArrayRef)ary, i);
	    r.location = i;
	    r.length = --n - i;
	    changed = true;
	}
    }
    if (!changed) {
	if (rb_block_given_p()) {
	    return rb_yield(item);
	}
	return Qnil;
    }
    return item;
}

VALUE
rb_ary_delete(VALUE ary, VALUE item)
{
    return rb_ary_delete_imp(ary, 0, item);
}

VALUE
rb_ary_delete_at(VALUE ary, long pos)
{
    const long len = RARRAY_LEN(ary);
    if (pos >= len) {
	return Qnil;
    }
    if (pos < 0) {
	pos += len;
	if (pos < 0) {
	    return Qnil;
	}
    }

    rb_ary_modify(ary);
    if (IS_RARY(ary)) {
	return rary_erase(RARY(ary), pos, 1);
    }
    else {
	VALUE del = RARRAY_AT(ary, pos);
	CFArrayRemoveValueAtIndex((CFMutableArrayRef)ary, pos);
	return del;
    }
}

/*
 *  call-seq:
 *     array.delete_at(index)  -> obj or nil
 *  
 *  Deletes the element at the specified index, returning that element,
 *  or <code>nil</code> if the index is out of range. See also
 *  <code>Array#slice!</code>.
 *     
 *     a = %w( ant bat cat dog )
 *     a.delete_at(2)    #=> "cat"
 *     a                 #=> ["ant", "bat", "dog"]
 *     a.delete_at(99)   #=> nil
 */

static VALUE
rb_ary_delete_at_m(VALUE ary, SEL sel, VALUE pos)
{
    return rb_ary_delete_at(ary, NUM2LONG(pos));
}

/*
 *  call-seq:
 *     array.slice!(index)         -> obj or nil
 *     array.slice!(start, length) -> sub_array or nil
 *     array.slice!(range)         -> sub_array or nil 
 *  
 *  Deletes the element(s) given by an index (optionally with a length)
 *  or by a range. Returns the deleted object, subarray, or
 *  <code>nil</code> if the index is out of range.
 *     
 *     a = [ "a", "b", "c" ]
 *     a.slice!(1)     #=> "b"
 *     a               #=> ["a", "c"]
 *     a.slice!(-1)    #=> "c"
 *     a               #=> ["a"]
 *     a.slice!(100)   #=> nil
 *     a               #=> ["a"]
 */

static VALUE
rb_ary_slice_bang(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE arg1, arg2;
    long pos, len;

    rb_ary_modify(ary);
    const long alen = RARRAY_LEN(ary);
    if (rb_scan_args(argc, argv, "11", &arg1, &arg2) == 2) {
	pos = NUM2LONG(arg1);
	len = NUM2LONG(arg2);
delete_pos_len:
	if (pos < 0) {
	    pos = alen + pos;
	    if (pos < 0) {
		return Qnil;
	    }
	}
	if (alen < len || alen < pos + len) {
	    len = alen - pos;
	}
	arg2 = rb_ary_subseq(ary, pos, len);
	rb_ary_splice(ary, pos, len, Qundef);	/* Qnil/rb_ary_new2(0) */
	return arg2;
    }

    if (!FIXNUM_P(arg1)) {
	switch (rb_range_beg_len(arg1, &pos, &len, RARRAY_LEN(ary), 0)) {
	    case Qtrue:
		/* valid range */
		goto delete_pos_len;
	    case Qnil:
		/* invalid range */
		return Qnil;
	    default:
		/* not a range */
		break;
	}
    }

    return rb_ary_delete_at(ary, NUM2LONG(arg1));
}

/*
 *  call-seq:
 *     array.reject! {|item| block }  -> array or nil
 *  
 *  Equivalent to <code>Array#delete_if</code>, deleting elements from
 *  _self_ for which the block evaluates to true, but returns
 *  <code>nil</code> if no changes were made. Also see
 *  <code>Enumerable#reject</code>.
 */

static VALUE
rb_ary_reject_bang(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);

    rb_ary_modify(ary);

    long n = RARRAY_LEN(ary);
    const long orign = n;
    long i1, i2;
    for (i1 = i2 = 0; i1 < n; i1++) {
	VALUE v = RARRAY_AT(ary, i1);
	VALUE v2 = rb_yield(v);
	RETURN_IF_BROKEN();
	if (!RTEST(v2)) {
	    continue;
	}
	rb_ary_erase(ary, i1);	
	n--;
	i1--;
    }

    if (n == orign) {
	return Qnil;
    }
    return ary;
}

/*
 *  call-seq:
 *     array.reject {|item| block }  -> an_array
 *  
 *  Returns a new array containing the items in _self_
 *  for which the block is not true.
 */

static VALUE
rb_ary_reject(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);
    ary = rb_ary_dup(ary);
    rb_ary_reject_bang(ary, 0);
    return ary;
}

/*
 *  call-seq:
 *     array.delete_if {|item| block }  -> array
 *  
 *  Deletes every element of <i>self</i> for which <i>block</i> evaluates
 *  to <code>true</code>.
 *     
 *     a = [ "a", "b", "c" ]
 *     a.delete_if {|x| x >= "b" }   #=> ["a"]
 */

static VALUE
rb_ary_delete_if(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);
    rb_ary_reject_bang(ary, 0);
    return ary;
}

static VALUE
take_i(VALUE val, VALUE *args, int argc, VALUE *argv)
{
    if (args[1]-- == 0) {
	rb_iter_break();
    }
    if (argc > 1) {
	val = rb_ary_new4(argc, argv);
    }
    rb_ary_push(args[0], val);
    return Qnil;
}

static VALUE
take_items(VALUE obj, long n)
{
    VALUE result = rb_check_array_type(obj);
    VALUE args[2];

    if (!NIL_P(result)) {
	return rb_ary_subseq(result, 0, n);
    }
    result = rb_ary_new2(n);
    args[0] = result;
    args[1] = (VALUE)n;
    
    rb_objc_block_call(obj, selEach, cacheEach, 0, 0,
	    (VALUE(*)(ANYARGS))take_i, (VALUE)args);

    return result;
}

/*
 *  call-seq:
 *     array.zip(arg, ...)                   -> an_array
 *     array.zip(arg, ...) {| arr | block }  -> nil
 *  
 *  Converts any arguments to arrays, then merges elements of
 *  <i>self</i> with corresponding elements from each argument. This
 *  generates a sequence of <code>self.size</code> <em>n</em>-element
 *  arrays, where <em>n</em> is one more that the count of arguments. If
 *  the size of any argument is less than <code>enumObj.size</code>,
 *  <code>nil</code> values are supplied. If a block given, it is
 *  invoked for each output array, otherwise an array of arrays is
 *  returned.
 *     
 *     a = [ 4, 5, 6 ]
 *     b = [ 7, 8, 9 ]
 *     [1,2,3].zip(a, b)      #=> [[1, 4, 7], [2, 5, 8], [3, 6, 9]]
 *     [1,2].zip(a,b)         #=> [[1, 4, 7], [2, 5, 8]]
 *     a.zip([1,2],[8])       #=> [[4,1,8], [5,2,nil], [6,nil,nil]]
 */

static VALUE
rb_ary_zip(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    const long len = RARRAY_LEN(ary);
    for (int i = 0; i < argc; i++) {
	argv[i] = take_items(argv[i], len);
    }
    VALUE result = Qnil;
    if (!rb_block_given_p()) {
	result = rb_ary_new2(len);
    }

    for (int i = 0; i < RARRAY_LEN(ary); i++) {
	VALUE tmp = rb_ary_new2(argc + 1);

	rb_ary_push(tmp, rb_ary_elt(ary, i));
	for (int j = 0; j < argc; j++) {
	    VALUE item = rb_ary_elt(argv[j], i);
	    rb_ary_push(tmp, item);
	}
	if (NIL_P(result)) {
	    rb_yield(tmp);
	    RETURN_IF_BROKEN();
	}
	else {
	    rb_ary_push(result, tmp);
	}
    }
    return result;
}

/*
 *  call-seq:
 *     array.transpose -> an_array
 *  
 *  Assumes that <i>self</i> is an array of arrays and transposes the
 *  rows and columns.
 *     
 *     a = [[1,2], [3,4], [5,6]]
 *     a.transpose   #=> [[1, 3, 5], [2, 4, 6]]
 */

static VALUE
rb_ary_transpose(VALUE ary, SEL sel)
{
    const long alen = RARRAY_LEN(ary);
    if (alen == 0) {
	return rb_ary_dup(ary);
    }

    long elen = -1;
    VALUE result = Qnil;
    for (long i = 0; i < alen; i++) {
	VALUE tmp = to_ary(rb_ary_elt(ary, i));
	if (elen < 0) {		/* first element */
	    elen = RARRAY_LEN(tmp);
	    result = rb_ary_new2(elen);
	    for (long j = 0; j < elen; j++) {
		rb_ary_store(result, j, rb_ary_new2(alen));
	    }
	}
	else if (elen != RARRAY_LEN(tmp)) {
	    rb_raise(rb_eIndexError, "element size differs (%ld should be %ld)",
		     RARRAY_LEN(tmp), elen);
	}
	for (long j = 0; j < elen; j++) {
	    rb_ary_store(rb_ary_elt(result, j), i, rb_ary_elt(tmp, j));
	}
    }
    return result;
}

/*
 *  call-seq:
 *     array.replace(other_array)  -> array
 *  
 *  Replaces the contents of <i>self</i> with the contents of
 *  <i>other_array</i>, truncating or expanding if necessary.
 *     
 *     a = [ "a", "b", "c", "d", "e" ]
 *     a.replace([ "x", "y", "z" ])   #=> ["x", "y", "z"]
 *     a                              #=> ["x", "y", "z"]
 */

VALUE
rb_ary_replace(VALUE copy, VALUE orig)
{
    orig = to_ary(orig);
    rb_ary_modify(copy);
    if (copy == orig) {
	return copy;
    }

    if (IS_RARY(copy) && IS_RARY(orig)) {
	rary_clear(RARY(copy));
	rary_concat(RARY(copy), RARY(orig), 0, RARY(orig)->len);
    }
    else {
	CFArrayRemoveAllValues((CFMutableArrayRef)copy);
	CFArrayAppendArray((CFMutableArrayRef)copy,
		(CFArrayRef)orig,
		CFRangeMake(0, RARRAY_LEN(orig)));
    }

    return copy;
}

static VALUE
rb_ary_replace_imp(VALUE copy, SEL sel, VALUE orig)
{
    return rb_ary_replace(copy, orig);
}

/* 
 *  call-seq:
 *     array.clear    ->  array
 *
 *  Removes all elements from _self_.
 *
 *     a = [ "a", "b", "c", "d", "e" ]
 *     a.clear    #=> [ ]
 */

static VALUE
rb_ary_clear_imp(VALUE ary, SEL sel)
{
    rb_ary_modify(ary);
    if (IS_RARY(ary)) {
	rary_clear(RARY(ary));
    }
    else {
	CFArrayRemoveAllValues((CFMutableArrayRef)ary);
    }
    return ary;
}

VALUE
rb_ary_clear(VALUE ary)
{
    return rb_ary_clear_imp(ary, 0);
}

/*
 *  call-seq:
 *     array.fill(obj)                                -> array
 *     array.fill(obj, start [, length])              -> array
 *     array.fill(obj, range )                        -> array
 *     array.fill {|index| block }                    -> array
 *     array.fill(start [, length] ) {|index| block } -> array
 *     array.fill(range) {|index| block }             -> array
 *  
 *  The first three forms set the selected elements of <i>self</i> (which
 *  may be the entire array) to <i>obj</i>. A <i>start</i> of
 *  <code>nil</code> is equivalent to zero. A <i>length</i> of
 *  <code>nil</code> is equivalent to <i>self.length</i>. The last three
 *  forms fill the array with the value of the block. The block is
 *  passed the absolute index of each element to be filled.
 *     
 *     a = [ "a", "b", "c", "d" ]
 *     a.fill("x")              #=> ["x", "x", "x", "x"]
 *     a.fill("z", 2, 2)        #=> ["x", "x", "z", "z"]
 *     a.fill("y", 0..1)        #=> ["y", "y", "z", "z"]
 *     a.fill {|i| i*i}         #=> [0, 1, 4, 9]
 *     a.fill(-2) {|i| i*i*i}   #=> [0, 1, 8, 27]
 */

static VALUE
rb_ary_fill(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE item, arg1, arg2;
    const long n = RARRAY_LEN(ary);
    bool block_p = false;
    if (rb_block_given_p()) {
	block_p = true;
	rb_scan_args(argc, argv, "02", &arg1, &arg2);
	argc += 1;		/* hackish */
    }
    else {
	rb_scan_args(argc, argv, "12", &item, &arg1, &arg2);
    }

    long beg = 0, end = 0, len = 0;
    switch (argc) {
	case 1:
	    beg = 0;
	    len = n;
	    break;
	case 2:
	    if (rb_range_beg_len(arg1, &beg, &len, n, 1)) {
		break;
	    }
	    /* fall through */
	case 3:
	    beg = NIL_P(arg1) ? 0 : NUM2LONG(arg1);
	    if (beg < 0) {
		beg = n + beg;
		if (beg < 0) {
		    beg = 0;
		}
	    }
	    len = NIL_P(arg2) ? n - beg : NUM2LONG(arg2);
	    break;
    }

    rb_ary_modify(ary);
    if (len < 0) {
	return ary;
    }
    end = beg + len;
    if (end < 0) {
	rb_raise(rb_eArgError, "argument too big");
    }

    if (block_p) {
	for (long i = beg; i < end; i++) {
	    VALUE v = rb_yield(LONG2NUM(i));
	    RETURN_IF_BROKEN();
	    rb_ary_store(ary, i, v);
	}
    }
    else {
	for (long i = beg; i < end; i++) {
	    rb_ary_store(ary, i, item);
	}
    }
    return ary;
}

/* 
 *  call-seq:
 *     array + other_array   -> an_array
 *
 *  Concatenation---Returns a new array built by concatenating the
 *  two arrays together to produce a third array.
 * 
 *     [ 1, 2, 3 ] + [ 4, 5 ]    #=> [ 1, 2, 3, 4, 5 ]
 */

VALUE
rb_ary_concat(VALUE x, VALUE y)
{
    if (IS_RARY(x) && IS_RARY(y)) {
	rary_concat(RARY(x), RARY(y), 0, RARY(y)->len);
    }
    else {
	const size_t len = RARRAY_LEN(y);
	CFArrayAppendArray((CFMutableArrayRef)x, (CFArrayRef)y,
		CFRangeMake(0, len));    
    }
    return x;
}

static VALUE
rb_ary_plus_imp(VALUE x, SEL sel, VALUE y)
{
    VALUE z;

    y = to_ary(y);
    z = rb_ary_new2(0);

    rary_reserve(RARY(z), RARRAY_LEN(x) + RARRAY_LEN(y));

    rb_ary_concat(z, x);
    rb_ary_concat(z, y);

    return z;
}

VALUE
rb_ary_plus(VALUE x, VALUE y)
{
    return rb_ary_plus_imp(x, 0, y);
}

/* 
 *  call-seq:
 *     array.concat(other_array)   ->  array
 *
 *  Appends the elements in other_array to _self_.
 *  
 *     [ "a", "b" ].concat( ["c", "d"] ) #=> [ "a", "b", "c", "d" ]
 */

static VALUE
rb_ary_concat_imp(VALUE x, SEL sel, VALUE y)
{
    rb_ary_modify(x);
    y = to_ary(y);
    rb_ary_concat(x, y);
    return x;
}

/* 
 *  call-seq:
 *     array * int     ->    an_array
 *     array * str     ->    a_string
 *
 *  Repetition---With a String argument, equivalent to
 *  self.join(str). Otherwise, returns a new array
 *  built by concatenating the _int_ copies of _self_.
 *
 *
 *     [ 1, 2, 3 ] * 3    #=> [ 1, 2, 3, 1, 2, 3, 1, 2, 3 ]
 *     [ 1, 2, 3 ] * ","  #=> "1,2,3"
 *
 */

static VALUE
rb_ary_times(VALUE ary, SEL sel, VALUE times)
{
    VALUE tmp = rb_check_string_type(times);
    if (!NIL_P(tmp)) {
	return rb_ary_join(ary, tmp);
    }

    const long len = NUM2LONG(times);
    if (len < 0) {
	rb_raise(rb_eArgError, "negative argument");
    }
    VALUE ary2 = ary_new(rb_obj_class(ary), 0);
    if (len > 0) {
	const long n = RARRAY_LEN(ary);
	if (LONG_MAX/len < n) {
	    rb_raise(rb_eArgError, "argument too big");
	}

	for (long i = 0; i < len; i++) {
	    rb_ary_concat(ary2, ary);
	}
    }

    if (OBJ_TAINTED(ary)) {
	OBJ_TAINT(ary2);
    }

    if (OBJ_UNTRUSTED(ary)) {
	OBJ_UNTRUST(ary2);
    }

    return ary2;
}

/* 
 *  call-seq:
 *     array.assoc(obj)   ->  an_array  or  nil
 *
 *  Searches through an array whose elements are also arrays
 *  comparing _obj_ with the first element of each contained array
 *  using obj.==.
 *  Returns the first contained array that matches (that
 *  is, the first associated array),
 *  or +nil+ if no match is found.
 *  See also <code>Array#rassoc</code>.
 *
 *     s1 = [ "colors", "red", "blue", "green" ]
 *     s2 = [ "letters", "a", "b", "c" ]
 *     s3 = "foo"
 *     a  = [ s1, s2, s3 ]
 *     a.assoc("letters")  #=> [ "letters", "a", "b", "c" ]
 *     a.assoc("foo")      #=> nil
 */

static VALUE
rb_ary_assoc(VALUE ary, SEL sel, VALUE key)
{
    for (long i = 0; i < RARRAY_LEN(ary); ++i) {
	VALUE v = rb_check_array_type(RARRAY_AT(ary, i));
	if (!NIL_P(v) && RARRAY_LEN(v) > 0 && rb_equal(RARRAY_AT(v, 0), key)) {
	    return v;
	}
    }
    return Qnil;
}

/*
 *  call-seq:
 *     array.rassoc(obj) -> an_array or nil
 *  
 *  Searches through the array whose elements are also arrays. Compares
 *  _obj_ with the second element of each contained array using
 *  <code>==</code>. Returns the first contained array that matches. See
 *  also <code>Array#assoc</code>.
 *     
 *     a = [ [ 1, "one"], [2, "two"], [3, "three"], ["ii", "two"] ]
 *     a.rassoc("two")    #=> [2, "two"]
 *     a.rassoc("four")   #=> nil
 */

static VALUE
rb_ary_rassoc(VALUE ary, SEL sel, VALUE value)
{
    for (long i = 0; i < RARRAY_LEN(ary); ++i) {
	VALUE v = RARRAY_AT(ary, i);
	if (TYPE(v) == T_ARRAY && RARRAY_LEN(v) > 1
	    && rb_equal(RARRAY_AT(v, 1), value)) {
	    return v;
	}
    }
    return Qnil;
}

/* 
 *  call-seq:
 *     array == other_array   ->   bool
 *
 *  Equality---Two arrays are equal if they contain the same number
 *  of elements and if each element is equal to (according to
 *  Object.==) the corresponding element in the other array.
 *
 *     [ "a", "c" ]    == [ "a", "c", 7 ]     #=> false
 *     [ "a", "c", 7 ] == [ "a", "c", 7 ]     #=> true
 *     [ "a", "c", 7 ] == [ "a", "d", "f" ]   #=> false
 *
 */

VALUE
rb_ary_equal(VALUE ary1, VALUE ary2)
{
    if (IS_RARY(ary1) && IS_RARY(ary2)) {
	if (RARY(ary1)->len != RARY(ary2)->len) {
	    return Qfalse;
	}
	for (size_t i = 0; i < RARY(ary1)->len; i++) {
	    VALUE item1 = rary_elt(RARY(ary1), i);
	    VALUE item2 = rary_elt(RARY(ary2), i);

	    if ((FIXFLOAT_P(item1) && isnan(FIXFLOAT2DBL(item1)))
		|| FIXFLOAT_P(item2) && isnan(FIXFLOAT2DBL(item2))) {
		return Qfalse;
	    }

	    if (rb_equal_fast(item1, item2) == Qfalse) {
		return Qfalse;
	    }
	}
	return Qtrue;
    }
    return CFEqual((CFTypeRef)ary1, (CFTypeRef)ary2) ? Qtrue : Qfalse;
}

static VALUE
rb_ary_equal_imp(VALUE ary1, SEL sel, VALUE ary2)
{
    if (ary1 == ary2) {
	return Qtrue;
    }
    if (TYPE(ary2) != T_ARRAY) {
	if (!rb_vm_respond_to(ary2, selToAry, true)) {
	    return Qfalse;
	}
	return rb_equal(ary2, ary1);
    }
    return rb_ary_equal(ary1, ary2);
}

static VALUE
recursive_eql(VALUE ary1, VALUE ary2, int recur)
{
    if (recur) {
	return Qfalse;
    }
    for (long i = 0; i < RARRAY_LEN(ary1); i++) {
	if (!rb_eql(rb_ary_elt(ary1, i), rb_ary_elt(ary2, i)))
	    return Qfalse;
    }
    return Qtrue;
}

/*
 *  call-seq:
 *     array.eql?(other)  -> true or false
 *
 *  Returns <code>true</code> if _array_ and _other_ are the same object,
 *  or are both arrays with the same content.
 */

static VALUE
rb_ary_eql(VALUE ary1, SEL sel, VALUE ary2)
{
    if (ary1 == ary2) {
	return Qtrue;
    }
    if (TYPE(ary2) != T_ARRAY) {
	return Qfalse;
    }
    if (RARRAY_LEN(ary1) != RARRAY_LEN(ary2)) {
	return Qfalse;
    }
    return rb_exec_recursive(recursive_eql, ary1, ary2);
}

/*
 *  call-seq:
 *     array.include?(obj)   -> true or false
 *  
 *  Returns <code>true</code> if the given object is present in
 *  <i>self</i> (that is, if any object <code>==</code> <i>anObject</i>),
 *  <code>false</code> otherwise.
 *     
 *     a = [ "a", "b", "c" ]
 *     a.include?("b")   #=> true
 *     a.include?("z")   #=> false
 */

static VALUE
rb_ary_includes_imp(VALUE ary, SEL sel, VALUE item)
{
    if (IS_RARY(ary)) {
	if (RARY(ary)->len == 0) {
	    return Qfalse;
	}
	return rary_index_of_item(RARY(ary), 0, item) != NOT_FOUND
	    ? Qtrue : Qfalse;
    }
    else {
	const long len = RARRAY_LEN(ary);
	const void *ocitem = RB2OC(item);
	return CFArrayContainsValue((CFArrayRef)ary, CFRangeMake(0, len),
		ocitem) ? Qtrue : Qfalse;
    }
}

VALUE
rb_ary_includes(VALUE ary, VALUE item)
{
    return rb_ary_includes_imp(ary, 0, item);
}

static VALUE
recursive_cmp(VALUE ary1, VALUE ary2, int recur)
{
    if (recur) {
	return Qnil;
    }

    long len = RARRAY_LEN(ary1);
    if (len > RARRAY_LEN(ary2)) {
	len = RARRAY_LEN(ary2);
    }

    for (long i = 0; i < len; i++) {
	VALUE v = rb_objs_cmp(rb_ary_elt(ary1, i), rb_ary_elt(ary2, i));
	if (v != INT2FIX(0)) {
	    return v;
	}
    }
    return Qundef;
}

/* 
 *  call-seq:
 *     array <=> other_array   ->  -1, 0, +1
 *
 *  Comparison---Returns an integer (-1, 0,
 *  or +1) if this array is less than, equal to, or greater than
 *  other_array.  Each object in each array is compared
 *  (using <=>). If any value isn't
 *  equal, then that inequality is the return value. If all the
 *  values found are equal, then the return is based on a
 *  comparison of the array lengths.  Thus, two arrays are
 *  ``equal'' according to <code>Array#<=></code> if and only if they have
 *  the same length and the value of each element is equal to the
 *  value of the corresponding element in the other array.
 *  
 *     [ "a", "a", "c" ]    <=> [ "a", "b", "c" ]   #=> -1
 *     [ 1, 2, 3, 4, 5, 6 ] <=> [ 1, 2 ]            #=> +1
 *
 */

static VALUE
rb_ary_cmp(VALUE ary1, SEL sel, VALUE ary2)
{
    ary2 = to_ary(ary2);
    if (ary1 == ary2) {
	return INT2FIX(0);
    }

    VALUE v = rb_exec_recursive(recursive_cmp, ary1, ary2);
    if (v != Qundef) {
	return v;
    }
    
    const long len = RARRAY_LEN(ary1) - RARRAY_LEN(ary2);
    return len == 0 ? INT2FIX(0) : len > 0 ? INT2FIX(1) : INT2FIX(-1);
}

static VALUE
ary_make_hash(VALUE ary1, VALUE ary2)
{
    VALUE hash = rb_hash_new();
    for (long i = 0; i < RARRAY_LEN(ary1); i++) {
	rb_hash_aset(hash, RARRAY_AT(ary1, i), Qtrue);
    }
    if (ary2) {
	for (long i = 0; i < RARRAY_LEN(ary2); i++) {
	    rb_hash_aset(hash, RARRAY_AT(ary2, i), Qtrue);
	}
    }
    return hash;
}

/* 
 *  call-seq:
 *     array - other_array    -> an_array
 *
 *  Array Difference---Returns a new array that is a copy of
 *  the original array, removing any items that also appear in
 *  other_array. (If you need set-like behavior, see the
 *  library class Set.)
 *
 *     [ 1, 1, 2, 2, 3, 3, 4, 5 ] - [ 1, 2, 4 ]  #=>  [ 3, 3, 5 ]
 */

static VALUE
rb_ary_diff(VALUE ary1, SEL sel, VALUE ary2)
{
    ary2 = to_ary(ary2);
    const long ary1_len = RARRAY_LEN(ary1);
    const long ary2_len = RARRAY_LEN(ary2);

    VALUE ary3 = rb_ary_new();

    if (ary2_len == 0) {
	rb_ary_concat(ary3, ary1);	
	return ary3;
    }

    rary_reserve(RARY(ary3), ary1_len);

    if (ary1_len < 100 && ary2_len < 100 && IS_RARY(ary1) && IS_RARY(ary2)) {
	for (long i = 0; i < ary1_len; i++) {
	    VALUE elem = rary_elt(RARY(ary1), i);
	    if (rary_index_of_item(RARY(ary2), 0, elem) == NOT_FOUND) {
		rary_append(RARY(ary3), elem);
	    }
	}
    }
    else {
	VALUE hash = ary_make_hash(ary2, 0);

	for (long i = 0; i < ary1_len; i++) {
	    VALUE v = RARRAY_AT(ary1, i);
	    if (rb_hash_has_key(hash, v) == Qfalse) {
		rb_ary_push(ary3, rb_ary_elt(ary1, i));
	    }
	}
    }
    return ary3;
}

/* 
 *  call-seq:
 *     array & other_array
 *
 *  Set Intersection---Returns a new array
 *  containing elements common to the two arrays, with no duplicates.
 *
 *     [ 1, 1, 3, 5 ] & [ 1, 2, 3 ]   #=> [ 1, 3 ]
 */

static void
filter_diff(VALUE ary1, VALUE ary3, VALUE hash)
{
    for (long i = 0; i < RARRAY_LEN(ary1); i++) {
	VALUE v = RARRAY_AT(ary1, i);
	if (rb_hash_delete_key(hash, v) != Qundef) {
	    rb_ary_push(ary3, v);
	}
    }
}

static VALUE
rb_ary_and(VALUE ary1, SEL sel, VALUE ary2)
{
    ary2 = to_ary(ary2);
    VALUE ary3 = rb_ary_new2(RARRAY_LEN(ary1) < RARRAY_LEN(ary2) ?
	    RARRAY_LEN(ary1) : RARRAY_LEN(ary2));
    VALUE hash = ary_make_hash(ary2, 0);
    if (RHASH_EMPTY_P(hash)) {
        return ary3;
    }
    filter_diff(ary1, ary3, hash);
    return ary3;
}

/* 
 *  call-seq:
 *     array | other_array     ->  an_array
 *
 *  Set Union---Returns a new array by joining this array with
 *  other_array, removing duplicates.
 *
 *     [ "a", "b", "c" ] | [ "c", "d", "a" ]
 *            #=> [ "a", "b", "c", "d" ]
 */

static VALUE
rb_ary_or(VALUE ary1, SEL sel, VALUE ary2)
{
    ary2 = to_ary(ary2);
    VALUE ary3 = rb_ary_new2(RARRAY_LEN(ary1) + RARRAY_LEN(ary2));
    VALUE hash = ary_make_hash(ary1, ary2);

    filter_diff(ary1, ary3, hash);
    filter_diff(ary2, ary3, hash);
    return ary3;
}

/*
 *  call-seq:
 *     array.uniq! -> array or nil
 *  
 *  Removes duplicate elements from _self_.
 *  Returns <code>nil</code> if no changes are made (that is, no
 *  duplicates are found).
 *     
 *     a = [ "a", "a", "b", "b", "c" ]
 *     a.uniq!   #=> ["a", "b", "c"]
 *     b = [ "a", "b", "c" ]
 *     b.uniq!   #=> nil
 */

static VALUE
rb_ary_uniq_bang(VALUE ary, SEL sel)
{
    rb_ary_modify(ary);
    long n = RARRAY_LEN(ary);
    bool changed = false;

    if (IS_RARY(ary)) {
	for (size_t i = 0; i < n; i++) {
	    VALUE item = rary_elt(RARY(ary), i);
	    size_t pos = i + 1;
	    while (pos < n && (pos = rary_index_of_item(RARY(ary), pos, item))
		    != NOT_FOUND) {
		rary_erase(RARY(ary), pos, 1);
		n--;
		changed = true;
	    }
	}
    }
    else {
	for (long i = 0; i < n; i++) {
	    const void *ocval = RB2OC(RARRAY_AT(ary, i));
	    CFRange r = CFRangeMake(i + 1, n - i - 1);
	    long idx = 0;
	    while ((idx = CFArrayGetFirstIndexOfValue((CFArrayRef)ary, 
			    r, ocval)) != -1) {
		CFArrayRemoveValueAtIndex((CFMutableArrayRef)ary, idx);
		r.location = idx;
		r.length = --n - idx;
		changed = true;
	    }
	}
    }

    if (!changed) {
	return Qnil;
    }
    return ary;
}

/*
 *  call-seq:
 *     array.uniq   -> an_array
 *  
 *  Returns a new array by removing duplicate values in <i>self</i>.
 *     
 *     a = [ "a", "a", "b", "b", "c" ]
 *     a.uniq   #=> ["a", "b", "c"]
 */

static VALUE
rb_ary_uniq(VALUE ary, SEL sel)
{
    ary = rb_ary_dup(ary);
    rb_ary_uniq_bang(ary, 0);
    return ary;
}

/* 
 *  call-seq:
 *     array.compact!    ->   array  or  nil
 *
 *  Removes +nil+ elements from array.
 *  Returns +nil+ if no changes were made.
 *
 *     [ "a", nil, "b", nil, "c" ].compact! #=> [ "a", "b", "c" ]
 *     [ "a", "b", "c" ].compact!           #=> nil
 */

static VALUE
rb_ary_compact_bang(VALUE ary, SEL sel)
{
    const long n = RARRAY_LEN(ary);
    rb_ary_delete(ary, Qnil);
    return RARRAY_LEN(ary) == n ? Qnil : ary;
}

/*
 *  call-seq:
 *     array.compact     ->  an_array
 *
 *  Returns a copy of _self_ with all +nil+ elements removed.
 *
 *     [ "a", nil, "b", nil, "c", nil ].compact
 *                       #=> [ "a", "b", "c" ]
 */

static VALUE
rb_ary_compact(VALUE ary, SEL sel)
{
    ary = rb_ary_dup(ary);
    rb_ary_compact_bang(ary, 0);
    return ary;
}

/*
 *  call-seq:
 *     array.count      -> int
 *     array.count(obj) -> int
 *     array.count { |item| block }  -> int
 *  
 *  Returns the number of elements.  If an argument is given, counts
 *  the number of elements which equals to <i>obj</i>.  If a block is
 *  given, counts the number of elements yielding a true value.
 *
 *     ary = [1, 2, 4, 2]
 *     ary.count             # => 4
 *     ary.count(2)          # => 2
 *     ary.count{|x|x%2==0}  # => 3
 *
 */

static VALUE
rb_ary_count(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    long n = 0;
 
    if (argc == 0) {
	long count = RARRAY_LEN(ary);

	if (!rb_block_given_p())
	    return LONG2NUM(count);

	for (long i = 0; i < count; i++) {
	    VALUE v = rb_yield(RARRAY_AT(ary, i)); 
	    RETURN_IF_BROKEN();
	    if (RTEST(v)) {
		n++;
	    }
	}
    }
    else {
	VALUE obj;
	const long count = RARRAY_LEN(ary);

	rb_scan_args(argc, argv, "1", &obj);
	if (rb_block_given_p()) {
	    rb_warn("given block not used");
	}

	if (IS_RARY(ary)) {
	    size_t pos = 0;
	    while ((pos = rary_index_of_item(RARY(ary), pos, obj))
		    != NOT_FOUND) {
		++n;
		if (++pos == count) {
		    break;
		}
	    }
	}
	else {
	    n = CFArrayGetCountOfValue((CFArrayRef)ary, CFRangeMake(0, count),
		    RB2OC(obj));
	}
    }

    return LONG2NUM(n);
}

static VALUE
flatten(VALUE ary, int level, int *modified)
{
    long i = 0;
    VALUE stack, result, tmp, elt;
    st_table *memo;
    st_data_t id;

    stack = rb_ary_new();
    result = ary_new(rb_class_of(ary), RARRAY_LEN(ary));
    memo = st_init_numtable();
    st_insert(memo, (st_data_t)ary, (st_data_t)Qtrue);
    *modified = 0;

    while (1) {
	while (i < RARRAY_LEN(ary)) {
	    elt = RARRAY_AT(ary, i++);
	    tmp = rb_check_array_type(elt);
	    if (NIL_P(tmp) || (level >= 0 && RARRAY_LEN(stack) / 2 >= level)) {
		rb_ary_push(result, elt);
	    }
	    else {
		*modified = 1;
		id = (st_data_t)tmp;
		if (st_lookup(memo, id, 0)) {
		    st_free_table(memo);
		    rb_raise(rb_eArgError, "tried to flatten recursive array");
		}
		st_insert(memo, id, (st_data_t)Qtrue);
		rb_ary_push(stack, ary);
		rb_ary_push(stack, LONG2NUM(i));
		ary = tmp;
		i = 0;
	    }
	}
	if (RARRAY_LEN(stack) == 0) {
	    break;
	}
	id = (st_data_t)ary;
	st_delete(memo, &id, 0);
	tmp = rb_ary_pop(stack);
	i = NUM2LONG(tmp);
	ary = rb_ary_pop(stack);
    }

    st_free_table(memo);

    return result;
}

/*
 *  call-seq:
 *     array.flatten! -> array or nil
 *     array.flatten!(level) -> array or nil
 *  
 *  Flattens _self_ in place.
 *  Returns <code>nil</code> if no modifications were made (i.e.,
 *  <i>array</i> contains no subarrays.)  If the optional <i>level</i>
 *  argument determines the level of recursion to flatten.
 *     
 *     a = [ 1, 2, [3, [4, 5] ] ]
 *     a.flatten!   #=> [1, 2, 3, 4, 5]
 *     a.flatten!   #=> nil
 *     a            #=> [1, 2, 3, 4, 5]
 *     a = [ 1, 2, [3, [4, 5] ] ]
 *     a.flatten!(1) #=> [1, 2, 3, [4, 5]]
 */

static VALUE
rb_ary_flatten_bang(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    int mod = 0, level = -1;
    VALUE result, lv;

    rb_scan_args(argc, argv, "01", &lv);
    rb_ary_modify(ary);
    if (!NIL_P(lv)) {
	level = NUM2INT(lv);
    }
    if (level == 0) {
	return ary;
    }

    result = flatten(ary, level, &mod);
    if (mod == 0) {
	return Qnil;
    }
    rb_ary_replace(ary, result);

    return ary;
}

/*
 *  call-seq:
 *     array.flatten -> an_array
 *     array.flatten(level) -> an_array
 *  
 *  Returns a new array that is a one-dimensional flattening of this
 *  array (recursively). That is, for every element that is an array,
 *  extract its elements into the new array.  If the optional
 *  <i>level</i> argument determines the level of recursion to flatten.
 *     
 *     s = [ 1, 2, 3 ]           #=> [1, 2, 3]
 *     t = [ 4, 5, 6, [7, 8] ]   #=> [4, 5, 6, [7, 8]]
 *     a = [ s, t, 9, 10 ]       #=> [[1, 2, 3], [4, 5, 6, [7, 8]], 9, 10]
 *     a.flatten                 #=> [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
 *     a = [ 1, 2, [3, [4, 5] ] ]
 *     a.flatten(1)              #=> [1, 2, 3, [4, 5]]
 */

static VALUE
rb_ary_flatten(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    int mod = 0, level = -1;
    VALUE result, lv;

    rb_scan_args(argc, argv, "01", &lv);
    if (!NIL_P(lv)) {
	level = NUM2INT(lv);
    }
    if (level == 0) {
	return ary;
    }

    result = flatten(ary, level, &mod);
    if (OBJ_TAINTED(ary)) {
	OBJ_TAINT(result);
    }

    return result;
}

/*
 *  call-seq:
 *     array.shuffle!        -> array or nil
 *  
 *  Shuffles elements in _self_ in place.
 */

static VALUE
rb_ary_shuffle_bang(VALUE ary, SEL sel)
{
    long i = RARRAY_LEN(ary);

    rb_ary_modify(ary);
    while (i) {
	const long j = rb_genrand_real() * i;
	VALUE elem = rb_ary_elt(ary, --i);
	rb_ary_store(ary, i, rb_ary_elt(ary, j));
	rb_ary_store(ary, j, elem);
    }
    return ary;
}

/*
 *  call-seq:
 *     array.shuffle -> an_array
 *  
 *  Returns a new array with elements of this array shuffled.
 *     
 *     a = [ 1, 2, 3 ]           #=> [1, 2, 3]
 *     a.shuffle                 #=> [2, 3, 1]
 */

static VALUE
rb_ary_shuffle(VALUE ary, SEL sel)
{
    ary = rb_ary_dup(ary);
    rb_ary_shuffle_bang(ary, 0);
    return ary;
}

/*
 *  call-seq:
 *     array.sample        -> obj
 *     array.sample(n)     -> an_array
 *  
 *  Choose a random element, or the random +n+ elements, from the array.
 *  If the array is empty, the first form returns <code>nil</code>, and the
 *  second form returns an empty array.
 */

static VALUE
rb_ary_sample(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE nv, result;
    long n, len, i, j, k, idx[10];

    len = RARRAY_LEN(ary); 
    if (argc == 0) {
	if (len == 0) {
	    return Qnil;
	}
	i = len == 1 ? 0 : rb_genrand_real() * len;
	return RARRAY_AT(ary, i);
    }
    rb_scan_args(argc, argv, "1", &nv);
    n = NUM2LONG(nv);
    len = RARRAY_LEN(ary); 
    if (n > len) {
	n = len;
    }
    switch (n) {
	case 0:
	    return rb_ary_new2(0);

	case 1:
	    nv = RARRAY_AT(ary, (long)(rb_genrand_real() * len));
	    return rb_ary_new4(1, &nv);

	case 2:
	    i = rb_genrand_real() * len;
	    j = rb_genrand_real() * (len - 1);
	    if (j >= i) {
		j++;
	    }
	    return rb_ary_new3(2, RARRAY_AT(ary, i), RARRAY_AT(ary, j));

	case 3:
	    i = rb_genrand_real() * len;
	    j = rb_genrand_real() * (len - 1);
	    k = rb_genrand_real() * (len - 2);
	    {
		long l = j, g = i;
		if (j >= i) {
		    l = i;
		    g = ++j;
		}
		if (k >= l && (++k >= g)) {
		    ++k;
		}
	    }
	    return rb_ary_new3(3, RARRAY_AT(ary, i), RARRAY_AT(ary, j),
		    RARRAY_AT(ary, k));
    }
    if ((unsigned long)n < sizeof(idx) / sizeof(idx[0])) {
	long sorted[sizeof(idx) / sizeof(idx[0])];
	sorted[0] = idx[0] = rb_genrand_real()*len;
	for (i = 1; i < n; i++) {
	    k = rb_genrand_real() * --len;
	    for (j = 0; j < i; ++j) {
		if (k < sorted[j]) {
		    break;
		}
		++k;
	    }
	    memmove(&sorted[j+1], &sorted[j], sizeof(sorted[0])*(i-j));
	    sorted[j] = idx[i] = k;
	}
	VALUE *elems = (VALUE *)alloca(sizeof(VALUE) * n);
	for (i = 0; i < n; i++) {
	    elems[i] = RARRAY_AT(ary, idx[i]);
	}
	result = rb_ary_new4(n, elems);
    }
    else {
	VALUE *elems = (VALUE *)alloca(sizeof(VALUE) * n);
	for (i = 0; i < n; i++) {
	    j = (long)(rb_genrand_real() * (len - i)) + i;
	    nv = RARRAY_AT(ary, j);
	    elems[i] = nv;
	}
	result = rb_ary_new4(n, elems);
    }

    return result;
}

/*
 *  call-seq:
 *     ary.cycle {|obj| block }
 *     ary.cycle(n) {|obj| block }
 *  
 *  Calls <i>block</i> for each element repeatedly _n_ times or
 *  forever if none or nil is given.  If a non-positive number is
 *  given or the array is empty, does nothing.  Returns nil if the
 *  loop has finished without getting interrupted.
 *     
 *     a = ["a", "b", "c"]
 *     a.cycle {|x| puts x }  # print, a, b, c, a, b, c,.. forever.
 *     a.cycle(2) {|x| puts x }  # print, a, b, c, a, b, c.
 *     
 */

static VALUE
rb_ary_cycle(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE nv = Qnil;
    rb_scan_args(argc, argv, "01", &nv);

    RETURN_ENUMERATOR(ary, argc, argv);

    long n;
    if (NIL_P(nv)) {
        n = -1;
    }
    else {
        n = NUM2LONG(nv);
        if (n <= 0) {
	    return Qnil;
	}
    }

    while (RARRAY_LEN(ary) > 0 && (n < 0 || 0 < n--)) {
        for (long i = 0; i < RARRAY_LEN(ary); i++) {
            rb_yield(RARRAY_AT(ary, i));
	    RETURN_IF_BROKEN();
        }
    }
    return Qnil;
}

/*
 * Recursively compute permutations of r elements of the set [0..n-1].
 * When we have a complete permutation of array indexes, copy the values
 * at those indexes into a new array and yield that array. 
 *
 * n: the size of the set 
 * r: the number of elements in each permutation
 * p: the array (of size r) that we're filling in
 * index: what index we're filling in now
 * used: an array of booleans: whether a given index is already used
 * values: the Ruby array that holds the actual values to permute
 */
static void
permute0(long n, long r, long *p, long index, int *used, VALUE values)
{
    long i,j;
    for (i = 0; i < n; i++) {
	if (used[i] == 0) {
	    p[index] = i;
	    if (index < r-1) {             /* if not done yet */
		used[i] = 1;               /* mark index used */
		permute0(n, r, p, index+1, /* recurse */
			 used, values);  
		used[i] = 0;               /* index unused */
	    }
	    else {
		/* We have a complete permutation of array indexes */
		/* Build a ruby array of the corresponding values */
		/* And yield it to the associated block */
		VALUE result = rb_ary_new2(r);
		for (j = 0; j < r; j++) {
		    rb_ary_store(result, j, RARRAY_AT(values, p[j]));
		}
		rb_yield(result);
	    }
	}
    }
}

/*
 *  call-seq:
 *     ary.permutation { |p| block }          -> array
 *     ary.permutation                        -> enumerator
 *     ary.permutation(n) { |p| block }       -> array
 *     ary.permutation(n)                     -> enumerator
 *  
 * When invoked with a block, yield all permutations of length <i>n</i>
 * of the elements of <i>ary</i>, then return the array itself.
 * If <i>n</i> is not specified, yield all permutations of all elements.
 * The implementation makes no guarantees about the order in which 
 * the permutations are yielded.
 *
 * When invoked without a block, return an enumerator object instead.
 * 
 * Examples:
 *
 *     a = [1, 2, 3]
 *     a.permutation.to_a     #=> [[1,2,3],[1,3,2],[2,1,3],[2,3,1],[3,1,2],[3,2,1]]
 *     a.permutation(1).to_a  #=> [[1],[2],[3]]
 *     a.permutation(2).to_a  #=> [[1,2],[1,3],[2,1],[2,3],[3,1],[3,2]]
 *     a.permutation(3).to_a  #=> [[1,2,3],[1,3,2],[2,1,3],[2,3,1],[3,1,2],[3,2,1]]
 *     a.permutation(0).to_a  #=> [[]] # one permutation of length 0
 *     a.permutation(4).to_a  #=> []   # no permutations of length 4
 */

static VALUE
rb_ary_permutation(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE num;
    long r, n, i;

    RETURN_ENUMERATOR(ary, argc, argv);   /* Return enumerator if no block */
    rb_scan_args(argc, argv, "01", &num);
    n = RARRAY_LEN(ary);                  /* Array length */
    r = NIL_P(num) ? n : NUM2LONG(num);   /* Permutation size from argument */

    if (r < 0 || n < r) { 
	/* no permutations: yield nothing */
    }
    else if (r == 0) { /* exactly one permutation: the zero-length array */
	rb_yield(rb_ary_new2(0));
    }
    else if (r == 1) { /* this is a special, easy case */
	for (i = 0; i < n; i++) {
	    rb_yield(rb_ary_new3(1, RARRAY_AT(ary, i)));
	    RETURN_IF_BROKEN();
	}
    }
    else {             /* this is the general case */
	long *p = (long *)alloca(n * sizeof(long));
	int *used = (int *)alloca(n * sizeof(int));
	VALUE ary0 = rb_ary_dup(ary);

	for (i = 0; i < n; i++) used[i] = 0; /* initialize array */

	permute0(n, r, p, 0, used, ary0); /* compute and yield permutations */
    }
    return ary;
}

static long
combi_len(long n, long k)
{
    long i, val = 1;

    if (k*2 > n) k = n-k;
    if (k == 0) return 1;
    if (k < 0) return 0;
    val = 1;
    for (i=1; i <= k; i++,n--) {
	long m = val;
	val *= n;
	if (val < m) {
	    rb_raise(rb_eRangeError, "too big for combination");
	}
	val /= i;
    }
    return val;
}

/*
 *  call-seq:
 *     ary.combination(n) { |c| block }    -> ary
 *     ary.combination(n)                  -> enumerator
 *  
 * When invoked with a block, yields all combinations of length <i>n</i> 
 * of elements from <i>ary</i> and then returns <i>ary</i> itself.
 * The implementation makes no guarantees about the order in which 
 * the combinations are yielded.
 *
 * When invoked without a block, returns an enumerator object instead.
 *     
 * Examples:
 *
 *     a = [1, 2, 3, 4]
 *     a.combination(1).to_a  #=> [[1],[2],[3],[4]]
 *     a.combination(2).to_a  #=> [[1,2],[1,3],[1,4],[2,3],[2,4],[3,4]]
 *     a.combination(3).to_a  #=> [[1,2,3],[1,2,4],[1,3,4],[2,3,4]]
 *     a.combination(4).to_a  #=> [[1,2,3,4]]
 *     a.combination(0).to_a  #=> [[]] # one combination of length 0
 *     a.combination(5).to_a  #=> []   # no combinations of length 5
 *     
 */

static VALUE
rb_ary_combination(VALUE ary, SEL sel, VALUE num)
{
    long n, i, len;

    n = NUM2LONG(num);
    RETURN_ENUMERATOR(ary, 1, &num);
    len = RARRAY_LEN(ary);
    if (n < 0 || len < n) {
	/* yield nothing */
    }
    else if (n == 0) {
	rb_yield(rb_ary_new2(0));
    }
    else if (n == 1) {
	for (i = 0; i < len; i++) {
	    rb_yield(rb_ary_new3(1, RARRAY_AT(ary, i)));
	    RETURN_IF_BROKEN();
	}
    }
    else {
	long *stack = (long *)alloca((n + 1) * sizeof(long));
	long nlen = combi_len(len, n);
	volatile VALUE cc = rb_ary_new2(n);
	long lev = 0;

	MEMZERO(stack, long, n);
	stack[0] = -1;
	for (i = 0; i < nlen; i++) {
	    rb_ary_store(cc, lev, RARRAY_AT(ary, stack[lev+1]));
	    for (lev++; lev < n; lev++) {
		stack[lev+1] = stack[lev]+1;
		rb_ary_store(cc, lev, RARRAY_AT(ary, stack[lev+1]));
	    }
	    rb_yield(rb_ary_dup(cc));
	    RETURN_IF_BROKEN();
	    do {
		stack[lev--]++;
	    } while (lev && (stack[lev+1]+n == len+lev+1));
	}
    }
    return ary;
}

/*
 *  call-seq:
 *     ary.product(other_ary, ...)
 *  
 *  Returns an array of all combinations of elements from all arrays.
 *  The length of the returned array is the product of the length
 *  of ary and the argument arrays
 *     
 *     [1,2,3].product([4,5])     # => [[1,4],[1,5],[2,4],[2,5],[3,4],[3,5]]
 *     [1,2].product([1,2])       # => [[1,1],[1,2],[2,1],[2,2]]
 *     [1,2].product([3,4],[5,6]) # => [[1,3,5],[1,3,6],[1,4,5],[1,4,6],
 *                                #     [2,3,5],[2,3,6],[2,4,5],[2,4,6]]
 *     [1,2].product()            # => [[1],[2]]
 *     [1,2].product([])          # => []
 */

static VALUE
rb_ary_product(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    int n = argc+1;    /* How many arrays we're operating on */
    VALUE *arrays = (VALUE *)alloca(n * sizeof(VALUE));; /* The arrays we're computing the product of */
    int *counters = (int *)alloca(n * sizeof(int)); /* The current position in each one */
    VALUE result;      /* The array we'll be returning */
    long i,j;
    long resultlen = 1;

    /* initialize the arrays of arrays */
    arrays[0] = ary;
    for (i = 1; i < n; i++) arrays[i] = to_ary(argv[i-1]);
    
    /* initialize the counters for the arrays */
    for (i = 0; i < n; i++) counters[i] = 0;

    /* Compute the length of the result array; return [] if any is empty */
    for (i = 0; i < n; i++) {
	long k = RARRAY_LEN(arrays[i]), l = resultlen;
	if (k == 0) return rb_ary_new2(0);
	resultlen *= k;
	if (resultlen < k || resultlen < l || resultlen / k != l) {
	    rb_raise(rb_eRangeError, "too big to product");
	}
    }

    /* Otherwise, allocate and fill in an array of results */
    result = rb_ary_new2(resultlen);
    for (i = 0; i < resultlen; i++) {
	int m;
	/* fill in one subarray */
	VALUE subarray = rb_ary_new2(n);
	for (j = 0; j < n; j++) {
	    rb_ary_push(subarray, rb_ary_entry(arrays[j], counters[j]));
	}

	/* put it on the result array */
	rb_ary_push(result, subarray);

	/*
	 * Increment the last counter.  If it overflows, reset to 0
	 * and increment the one before it.
	 */
	m = n-1;
	counters[m]++;
	while (m > 0 && counters[m] == RARRAY_LEN(arrays[m])) {
	    counters[m] = 0;
	    m--;
	    counters[m]++;
	}
    }

    return result;
}

/*
 *  call-seq:
 *     ary.take(n)               => array
 *  
 *  Returns first n elements from <i>ary</i>.
 *     
 *     a = [1, 2, 3, 4, 5, 0]
 *     a.take(3)             # => [1, 2, 3]
 *     
 */

static VALUE
rb_ary_take(VALUE obj, SEL sel, VALUE n)
{
    const long len = NUM2LONG(n);
    if (len < 0) {
	rb_raise(rb_eArgError, "attempt to take negative size");
    }
    return rb_ary_subseq(obj, 0, len);
}

/*
 *  call-seq:
 *     ary.take_while {|arr| block }   => array
 *  
 *  Passes elements to the block until the block returns nil or false,
 *  then stops iterating and returns an array of all prior elements.
 *     
 *     a = [1, 2, 3, 4, 5, 0]
 *     a.take_while {|i| i < 3 }   # => [1, 2]
 *     
 */

static VALUE
rb_ary_take_while(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);
    long i = 0;
    for (; i < RARRAY_LEN(ary); i++) {
	VALUE v = rb_yield(RARRAY_AT(ary, i));
	RETURN_IF_BROKEN();
	if (!RTEST(v)) {
	    break;
	}
    }
    return rb_ary_take(ary, 0, LONG2FIX(i));
}

/*
 *  call-seq:
 *     ary.drop(n)               => array
 *  
 *  Drops first n elements from <i>ary</i>, and returns rest elements
 *  in an array.
 *     
 *     a = [1, 2, 3, 4, 5, 0]
 *     a.drop(3)             # => [4, 5, 0]
 *     
 */

static VALUE
rb_ary_drop(VALUE ary, SEL sel, VALUE n)
{
    const long pos = NUM2LONG(n);
    if (pos < 0) {
	rb_raise(rb_eArgError, "attempt to drop negative size");
    }

    VALUE result = rb_ary_subseq(ary, pos, RARRAY_LEN(ary));
    if (result == Qnil) {
	result = rb_ary_new();
    }
    return result;
}

/*
 *  call-seq:
 *     ary.drop_while {|arr| block }   => array
 *  
 *  Drops elements up to, but not including, the first element for
 *  which the block returns nil or false and returns an array
 *  containing the remaining elements.
 *     
 *     a = [1, 2, 3, 4, 5, 0]
 *     a.drop_while {|i| i < 3 }   # => [3, 4, 5, 0]
 *     
 */

static VALUE
rb_ary_drop_while(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);
    long i = 0;
    for (; i < RARRAY_LEN(ary); i++) {
	VALUE v = rb_yield(RARRAY_AT(ary, i));
	RETURN_IF_BROKEN();
	if (!RTEST(v)) {
	    break;
	}
    }
    return rb_ary_drop(ary, 0, LONG2FIX(i));
}

#define PREPARE_RCV(x) \
    Class old = *(Class *)x; \
    *(Class *)x = (Class)rb_cCFArray;

#define RESTORE_RCV(x) \
      *(Class *)x = old;

bool
rb_objc_ary_is_pure(VALUE ary)
{
    return *(Class *)ary == (Class)rb_cRubyArray;
}

static CFIndex
imp_rb_array_count(void *rcv, SEL sel)
{
    CFIndex count;
    PREPARE_RCV(rcv);
    count = CFArrayGetCount((CFArrayRef)rcv);
    RESTORE_RCV(rcv);
    return count;
}

static const void *
imp_rb_array_objectAtIndex(void *rcv, SEL sel, CFIndex idx)
{
    const void *obj;
    PREPARE_RCV(rcv);
    obj = CFArrayGetValueAtIndex((CFArrayRef)rcv, idx);
    RESTORE_RCV(rcv);
    return obj;
}

static void
imp_rb_array_insertObjectAtIndex(void *rcv, SEL sel, void *obj, CFIndex idx)
{
    PREPARE_RCV(rcv);
    CFArrayInsertValueAtIndex((CFMutableArrayRef)rcv, idx, obj);
    RESTORE_RCV(rcv);
}

static void
imp_rb_array_removeObjectAtIndex(void *rcv, SEL sel, CFIndex idx)
{
    PREPARE_RCV(rcv);
    CFArrayRemoveValueAtIndex((CFMutableArrayRef)rcv, idx);
    RESTORE_RCV(rcv);
}

static void
imp_rb_array_replaceObjectAtIndexWithObject(void *rcv, SEL sel, CFIndex idx, 
    void *obj)
{
    PREPARE_RCV(rcv);
    CFArraySetValueAtIndex((CFMutableArrayRef)rcv, idx, obj);
    RESTORE_RCV(rcv);
}

static void
imp_rb_array_replaceObjectsInRangeWithObjectsCount(void *rcv, SEL sel,
    CFRange range, const void **objects, CFIndex count)
{
    PREPARE_RCV(rcv);
    CFArrayReplaceValues((CFMutableArrayRef)rcv, range, objects, count);
    RESTORE_RCV(rcv);
}

static void
imp_rb_array_addObject(void *rcv, SEL sel, void *obj)
{
    PREPARE_RCV(rcv);
    CFArrayAppendValue((CFMutableArrayRef)rcv, obj);
    RESTORE_RCV(rcv);
}

#if MAC_OS_X_VERSION_MAX_ALLOWED < 1060
static CFIndex
imp_rb_array_cfindexOfObjectInRange(void *rcv, SEL sel, void *obj, 
    CFRange range)
{
    CFIndex i;
    PREPARE_RCV(rcv);
    i = CFArrayGetFirstIndexOfValue((CFArrayRef)rcv, range, obj);
    RESTORE_RCV(rcv);
    return i;
}
#endif

void
rb_objc_install_array_primitives(Class klass)
{
    rb_objc_install_method2(klass, "count", (IMP)imp_rb_array_count);
    rb_objc_install_method2(klass, "objectAtIndex:",
	    (IMP)imp_rb_array_objectAtIndex);

    const bool is_mutable = class_getSuperclass(klass)
	== (Class)rb_cNSMutableArray;

    if (is_mutable) {
	rb_objc_install_method2(klass, "insertObject:atIndex:",
		(IMP)imp_rb_array_insertObjectAtIndex);
	rb_objc_install_method2(klass, "removeObjectAtIndex:",
		(IMP)imp_rb_array_removeObjectAtIndex);
	rb_objc_install_method2(klass, "replaceObjectAtIndex:withObject:", 
		(IMP)imp_rb_array_replaceObjectAtIndexWithObject);
	rb_objc_install_method2(klass,
		"replaceObjectsInRange:withObjects:count:",
		(IMP)imp_rb_array_replaceObjectsInRangeWithObjectsCount);
	rb_objc_install_method2(klass, "addObject:",
		(IMP)imp_rb_array_addObject);
    }

#if MAC_OS_X_VERSION_MAX_ALLOWED < 1060
    /* This is to work around a bug where CF will try to call an non-existing 
     * method. 
     */
    rb_objc_install_method2(klass, "_cfindexOfObject:range:",
	    (IMP)imp_rb_array_cfindexOfObjectInRange);
    Method m = class_getInstanceMethod(klass, 
	    sel_registerName("_cfindexOfObject:range:"));
    class_addMethod(klass, sel_registerName("_cfindexOfObject:inRange:"), 
	    method_getImplementation(m), method_getTypeEncoding(m));
#endif

    rb_objc_define_method(*(VALUE *)klass, "alloc", ary_alloc, 0);
}

static CFIndex
imp_rary_count(void *rcv, SEL sel)
{
    return RARY(rcv)->len;
}

static const void *
imp_rary_objectAtIndex(void *rcv, SEL sel, CFIndex idx)
{
    assert(idx < RARY(rcv)->len);
    return RB2OC(rary_elt(RARY(rcv), idx));
}

static void
imp_rary_insertObjectAtIndex(void *rcv, SEL sel, void *obj, CFIndex idx)
{
    rary_insert(RARY(rcv), idx, OC2RB(obj));
}

static void
imp_rary_removeObjectAtIndex(void *rcv, SEL sel, CFIndex idx)
{
    rary_erase(RARY(rcv), idx, 1);
}

static void
imp_rary_replaceObjectAtIndexWithObject(void *rcv, SEL sel, CFIndex idx, 
	void *obj)
{
    rary_store(RARY(rcv), idx, OC2RB(obj));
}

static void
imp_rary_addObject(void *rcv, SEL sel, void *obj)
{
    rary_append(RARY(rcv), OC2RB(obj));
}

/* Arrays are ordered, integer-indexed collections of any object. 
 * Array indexing starts at 0, as in C or Java.  A negative index is 
 * assumed to be relative to the end of the array---that is, an index of -1 
 * indicates the last element of the array, -2 is the next to last 
 * element in the array, and so on. 
 */

void
Init_Array(void)
{
    rb_cCFArray = (VALUE)objc_getClass("NSCFArray");
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
    rb_cNSArray0 = (VALUE)objc_getClass("__NSArray0");
#endif
    rb_const_set(rb_cObject, rb_intern("NSCFArray"), rb_cCFArray);
    rb_cArray = rb_cNSArray = (VALUE)objc_getClass("NSArray");
    rb_cNSMutableArray = (VALUE)objc_getClass("NSMutableArray");
    rb_set_class_path(rb_cNSMutableArray, rb_cObject, "NSMutableArray");
    rb_const_set(rb_cObject, rb_intern("Array"), rb_cNSMutableArray);

    rb_include_module(rb_cArray, rb_mEnumerable);

    rb_objc_define_method(*(VALUE *)rb_cArray, "[]", rb_ary_s_create, -1);
    rb_objc_define_method(*(VALUE *)rb_cArray, "try_convert", rb_ary_s_try_convert, 1);
    rb_objc_define_method(rb_cArray, "initialize", rb_ary_initialize, -1);
    rb_objc_define_method(rb_cArray, "initialize_copy", rb_ary_replace_imp, 1);

    rb_objc_define_method(rb_cArray, "to_s", rb_ary_inspect, 0);
    rb_objc_define_method(rb_cArray, "inspect", rb_ary_inspect, 0);
    rb_objc_define_method(rb_cArray, "to_a", rb_ary_to_a, 0);
    rb_objc_define_method(rb_cArray, "to_ary", rb_ary_to_ary_m, 0);
    rb_objc_define_method(rb_cArray, "frozen?",  rb_ary_frozen_imp, 0);

    rb_objc_define_method(rb_cArray, "==", rb_ary_equal_imp, 1);
    rb_objc_define_method(rb_cArray, "eql?", rb_ary_eql, 1);

    rb_objc_define_method(rb_cArray, "[]", rb_ary_aref, -1);
    rb_objc_define_method(rb_cArray, "[]=", rb_ary_aset, -1);
    rb_objc_define_method(rb_cArray, "at", rb_ary_at, 1);
    rb_objc_define_method(rb_cArray, "fetch", rb_ary_fetch, -1);
    rb_objc_define_method(rb_cArray, "first", rb_ary_first, -1);
    rb_objc_define_method(rb_cArray, "last", rb_ary_last, -1);
    rb_objc_define_method(rb_cArray, "concat", rb_ary_concat_imp, 1);
    rb_objc_define_method(rb_cArray, "<<", rb_ary_push_imp, 1);
    rb_objc_define_method(rb_cArray, "push", rb_ary_push_m, -1);
    rb_objc_define_method(rb_cArray, "pop", rb_ary_pop_m, -1);
    rb_objc_define_method(rb_cArray, "shift", rb_ary_shift_m, -1);
    rb_objc_define_method(rb_cArray, "unshift", rb_ary_unshift_m, -1);
    rb_objc_define_method(rb_cArray, "insert", rb_ary_insert_m, -1);
    rb_objc_define_method(rb_cArray, "each", rb_ary_each, 0);
    rb_objc_define_method(rb_cArray, "each_index", rb_ary_each_index, 0);
    rb_objc_define_method(rb_cArray, "reverse_each", rb_ary_reverse_each, 0);
    rb_objc_define_method(rb_cArray, "length", rb_ary_length, 0);
    rb_objc_define_method(rb_cArray, "size", rb_ary_length, 0);
    rb_objc_define_method(rb_cArray, "empty?", rb_ary_empty_p, 0);
    rb_objc_define_method(rb_cArray, "find_index", rb_ary_index, -1);
    rb_objc_define_method(rb_cArray, "index", rb_ary_index, -1);
    rb_objc_define_method(rb_cArray, "rindex", rb_ary_rindex, -1);
    rb_objc_define_method(rb_cArray, "join", rb_ary_join_m, -1);
    rb_objc_define_method(rb_cArray, "reverse", rb_ary_reverse_m, 0);
    rb_objc_define_method(rb_cArray, "reverse!", rb_ary_reverse_bang, 0);
    rb_objc_define_method(rb_cArray, "sort", rb_ary_sort_imp, 0);
    rb_objc_define_method(rb_cArray, "sort!", rb_ary_sort_bang_imp, 0);
    rb_objc_define_method(rb_cArray, "collect", rb_ary_collect, 0);
    rb_objc_define_method(rb_cArray, "collect!", rb_ary_collect_bang, 0);
    rb_objc_define_method(rb_cArray, "map", rb_ary_collect, 0);
    rb_objc_define_method(rb_cArray, "map!", rb_ary_collect_bang, 0);
    rb_objc_define_method(rb_cArray, "select", rb_ary_select, 0);
    rb_objc_define_method(rb_cArray, "values_at", rb_ary_values_at, -1);
    rb_objc_define_method(rb_cArray, "delete", rb_ary_delete_imp, 1);
    rb_objc_define_method(rb_cArray, "delete_at", rb_ary_delete_at_m, 1);
    rb_objc_define_method(rb_cArray, "delete_if", rb_ary_delete_if, 0);
    rb_objc_define_method(rb_cArray, "reject", rb_ary_reject, 0);
    rb_objc_define_method(rb_cArray, "reject!", rb_ary_reject_bang, 0);
    rb_objc_define_method(rb_cArray, "zip", rb_ary_zip, -1);
    rb_objc_define_method(rb_cArray, "transpose", rb_ary_transpose, 0);
    rb_objc_define_method(rb_cArray, "replace", rb_ary_replace_imp, 1);
    rb_objc_define_method(rb_cArray, "clear", rb_ary_clear_imp, 0);
    rb_objc_define_method(rb_cArray, "fill", rb_ary_fill, -1);
    rb_objc_define_method(rb_cArray, "include?", rb_ary_includes_imp, 1);
    rb_objc_define_method(rb_cArray, "<=>", rb_ary_cmp, 1);

    rb_objc_define_method(rb_cArray, "slice", rb_ary_aref, -1);
    rb_objc_define_method(rb_cArray, "slice!", rb_ary_slice_bang, -1);

    rb_objc_define_method(rb_cArray, "assoc", rb_ary_assoc, 1);
    rb_objc_define_method(rb_cArray, "rassoc", rb_ary_rassoc, 1);

    rb_objc_define_method(rb_cArray, "+", rb_ary_plus_imp, 1);
    rb_objc_define_method(rb_cArray, "*", rb_ary_times, 1);

    rb_objc_define_method(rb_cArray, "-", rb_ary_diff, 1);
    rb_objc_define_method(rb_cArray, "&", rb_ary_and, 1);
    rb_objc_define_method(rb_cArray, "|", rb_ary_or, 1);

    rb_objc_define_method(rb_cArray, "uniq", rb_ary_uniq, 0);
    rb_objc_define_method(rb_cArray, "uniq!", rb_ary_uniq_bang, 0);
    rb_objc_define_method(rb_cArray, "compact", rb_ary_compact, 0);
    rb_objc_define_method(rb_cArray, "compact!", rb_ary_compact_bang, 0);
    rb_objc_define_method(rb_cArray, "flatten", rb_ary_flatten, -1);
    rb_objc_define_method(rb_cArray, "flatten!", rb_ary_flatten_bang, -1);
    rb_objc_define_method(rb_cArray, "_count", rb_ary_count, -1);

    /* to maintain backwards compatibility with our /etc/irbrc file
     * TODO: fix /etc/irbrc to use #count, and remove this method.
     */
    rb_objc_define_method(rb_cArray, "nitems", rb_ary_count, -1);

    rb_objc_define_method(rb_cArray, "shuffle!", rb_ary_shuffle_bang, 0);
    rb_objc_define_method(rb_cArray, "shuffle", rb_ary_shuffle, 0);
    rb_objc_define_method(rb_cArray, "sample", rb_ary_sample, -1);
    rb_objc_define_method(rb_cArray, "cycle", rb_ary_cycle, -1);
    rb_objc_define_method(rb_cArray, "permutation", rb_ary_permutation, -1);
    rb_objc_define_method(rb_cArray, "combination", rb_ary_combination, 1);
    rb_objc_define_method(rb_cArray, "product", rb_ary_product, -1);

    rb_objc_define_method(rb_cArray, "take", rb_ary_take, 1);
    rb_objc_define_method(rb_cArray, "take_while", rb_ary_take_while, 0);
    rb_objc_define_method(rb_cArray, "drop", rb_ary_drop, 1);
    rb_objc_define_method(rb_cArray, "drop_while", rb_ary_drop_while, 0);

    /* to return mutable copies */
    rb_objc_define_method(rb_cArray, "dup", rb_ary_dup_imp, 0);
    rb_objc_define_method(rb_cArray, "clone", rb_ary_clone, 0);

    rb_cRubyArray = rb_define_class("RubyArray", rb_cNSMutableArray);
    rb_objc_define_method(*(VALUE *)rb_cRubyArray, "alloc", ary_alloc, 0);
    rb_objc_install_method2((Class)rb_cRubyArray, "count", (IMP)imp_rary_count);
    rb_objc_install_method2((Class)rb_cRubyArray, "objectAtIndex:",
	    (IMP)imp_rary_objectAtIndex);

    rb_objc_install_method2((Class)rb_cRubyArray, "insertObject:atIndex:",
	    (IMP)imp_rary_insertObjectAtIndex);
    rb_objc_install_method2((Class)rb_cRubyArray, "removeObjectAtIndex:",
	    (IMP)imp_rary_removeObjectAtIndex);
    rb_objc_install_method2((Class)rb_cRubyArray,
	    "replaceObjectAtIndex:withObject:", 
	    (IMP)imp_rary_replaceObjectAtIndexWithObject);
    rb_objc_install_method2((Class)rb_cRubyArray, "addObject:",
	    (IMP)imp_rary_addObject);
}
