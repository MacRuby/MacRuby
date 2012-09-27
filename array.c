/*
 * MacRuby implementation of Ruby 1.9's array.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include "macruby_internal.h"
#include "ruby/util.h"
#include "ruby/st.h"
#include "id.h"
#include "objc.h"
#include "ruby/node.h"
#include "vm.h"
#include "class.h"
#include "array.h"

VALUE rb_cRubyArray;

#define ARY_DEFAULT_SIZE 16

// RubyArray primitives.

void
rary_reserve(VALUE ary, size_t newlen)
{
    rb_ary_t *rary = RARY(ary); 
    if (rary->beg + newlen > rary->cap) {
	if (rary->beg > 0) {
	    if (rary->beg > newlen) {
		newlen = 0;
	    }
	    else {
		newlen -= rary->beg;
	    }
	    GC_MEMMOVE(&rary->elements[0],
		       &rary->elements[rary->beg],
		       sizeof(VALUE) * rary->len);
	    rary->beg = 0;
	}
	if (newlen > rary->cap) {
	    if (rary->cap > 0) {
		newlen *= 2;
	    }
	    if (rary->elements == NULL) {
		GC_WB(&rary->elements, xmalloc_ptrs(sizeof(VALUE) * newlen));
	    }
	    else {
		VALUE *new_elements = xrealloc(rary->elements,
			sizeof(VALUE) * newlen);
		if (new_elements != rary->elements) {
		    GC_WB(&rary->elements, new_elements);
		}
	    }
	    rary->cap = newlen;
	}
    }
}

static VALUE
rary_erase(VALUE ary, size_t idx, size_t len)
{
    assert(idx + len <= RARY(ary)->len);
    VALUE item = rary_elt(ary, idx);
    if (idx == 0) {
	for (size_t i = 0; i < len; i++) {
	    rary_elt_set(ary, i, Qnil);
	}
	if (len < RARY(ary)->len) {
	    RARY(ary)->beg += len;
	}
	else {
	    RARY(ary)->beg = 0;
	}
    }
    else {
	GC_MEMMOVE(&RARY(ary)->elements[RARY(ary)->beg + idx],
		   &RARY(ary)->elements[RARY(ary)->beg + idx + len],
		   sizeof(VALUE) * (RARY(ary)->len - idx - len));
	for (size_t i = 0; i < len; i++) {
	    rary_elt_set(ary, RARY(ary)->len - i - 1, Qnil);
	}
    }
    RARY(ary)->len -= len;
    return item;
}

static void
rary_resize(VALUE ary, size_t newlen)
{
    if (newlen > RARY(ary)->cap) {
	rary_reserve(ary, newlen);
    }
    for (size_t i = RARY(ary)->len; i < newlen; i++) {
	rary_elt_set(ary, i, Qnil);
    }
    RARY(ary)->len = newlen;
}

static void
rary_concat(VALUE ary, VALUE other, size_t beg, size_t len)
{
    rary_reserve(ary, RARY(ary)->len + len);
    if (IS_RARY(other)) {
	GC_MEMMOVE(&RARY(ary)->elements[RARY(ary)->beg + RARY(ary)->len],
		   &RARY(other)->elements[RARY(other)->beg + beg],
		   sizeof(VALUE) * len);
    }
    else {
	for (size_t i = 0; i < len; i++) {
	    rary_elt_set(ary, i + RARY(ary)->len,
		    rb_ary_elt(other, beg + i));
	}
    }
    RARY(ary)->len += len;
}

static void
rary_remove_all(rb_ary_t *ary)
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
    if (SYMBOL_P(x)) {
	return x == y ? Qtrue : Qfalse;
    }
    return rb_equal(x, y);
}

void
rb_mem_clear(register VALUE *mem, register long size)
{
    while (size--) {
	*mem++ = Qnil;
    }
}

static inline VALUE
rary_alloc(VALUE klass, SEL sel)
{
    assert(klass != 0);
    assert(rb_klass_is_rary(klass));

    NEWOBJ(ary, rb_ary_t);
    ary->basic.flags = 0;
    ary->basic.klass = klass;
    ary->beg = ary->len = ary->cap = 0;
    ary->elements = NULL;
    return (VALUE)ary;
}

static inline void
assert_ary_len(const long len)
{
    if (len < 0) {
	rb_raise(rb_eArgError, "negative array size (or size too big)");
    }
    if ((unsigned long)len > ARY_MAX_SIZE) {
	rb_raise(rb_eArgError, "array size too big");
    }
}

VALUE
rb_ary_new2(long len)
{
    assert_ary_len(len);

    VALUE ary;
    if (rb_cRubyArray != 0) {
	ary = rary_alloc(rb_cRubyArray, 0);
	rary_reserve(ary, len);
    }
    else {
	// RubyArray does not exist yet... fallback on an CFArray.
	ary = (VALUE)CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	CFMakeCollectable((void *)ary);
    }
    return ary;
}

VALUE
rb_ary_new(void)
{
    return rb_ary_new2(ARY_DEFAULT_SIZE);
}

VALUE
rb_ary_new3(long n, ...)
{
    VALUE ary = rb_ary_new2(n);

    if (n > 0) {
	va_list ar;
	va_start(ar, n);
	rary_reserve(ary, n);
	for (long i = 0; i < n; i++) {
	    rary_push(ary, va_arg(ar, VALUE));
	}
	va_end(ar);
    }

    return ary;
}

VALUE
rb_ary_new4(long n, const VALUE *elts)
{
    VALUE ary = rb_ary_new2(n);
    if (n > 0 && elts != NULL) {
	GC_MEMMOVE(rary_ptr(ary),
		   elts,
		   sizeof(VALUE) * n);
	RARY(ary)->len = n;
    }
    return ary;
}

VALUE
rb_assoc_new(VALUE car, VALUE cdr)
{
    VALUE elems[] = { car, cdr };
    return rb_ary_new4(2, elems);
}

VALUE
rb_check_array_type(VALUE ary)
{
    return rb_check_convert_type(ary, T_ARRAY, "Array", "to_ary");
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
rary_s_try_convert(VALUE dummy, SEL sel, VALUE ary)
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

static VALUE rary_replace(VALUE rcv, SEL sel, VALUE other);

static VALUE
rary_initialize(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    rary_modify(ary);

    if (argc ==  0) {
	if (rb_block_given_p()) {
	    rb_warning("given block not used");
	}
	rary_remove_all(RARY(ary));
	return ary;
    }

    VALUE size, val;
    rb_scan_args(argc, argv, "02", &size, &val);
    if (argc == 1 && !FIXNUM_P(size)) {
	val = rb_check_array_type(size);
	if (!NIL_P(val)) {
	    rary_replace(ary, 0, val);
	    return ary;
	}
    }

    const long len = NUM2LONG(size);
    assert_ary_len(len);
    rary_resize(ary, len);
    if (rb_block_given_p()) {
	if (argc == 2) {
	    rb_warn("block supersedes default value argument");
	}
	rary_remove_all(RARY(ary));
	for (long i = 0; i < len; i++) {
	    VALUE v = rb_yield(LONG2NUM(i));
	    if (BROKEN_VALUE() != Qundef) {
		return ary;
	    }
	    rary_push(ary, v);
	}
    }
    else {
	for (long i = 0; i < len; i++) {
	    rary_store(ary, i, val);
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
rary_s_create(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE ary = rary_alloc(klass, 0);
    if (argc < 0) {
	rb_raise(rb_eArgError, "negative array size");
    }
    rary_reserve(ary, argc);
    for (int i = 0; i < argc; i++) {
	rary_push(ary, argv[i]);
    }
    return ary;
}

void
rary_insert(VALUE ary, long idx, VALUE val)
{
    if (idx < 0) {
	idx += RARY(ary)->len;
	if (idx < 0) {
	    rb_raise(rb_eIndexError, "index %ld out of array",
		    idx - RARY(ary)->len);
	}
    }
    if (idx > RARY(ary)->len) {
	rary_resize(ary, idx + 1);
	rary_store(ary, idx, val);
    }
    else if (idx < RARY(ary)->len) {
	rary_reserve(ary, RARY(ary)->len + 1);
	GC_MEMMOVE(&RARY(ary)->elements[RARY(ary)->beg + idx + 1],
		   &RARY(ary)->elements[RARY(ary)->beg + idx],
		   sizeof(VALUE) * (RARY(ary)->len - idx));
	rary_elt_set(ary, idx, val);
	RARY(ary)->len++;
    }
    else {
	rary_push(ary, val);
    }
}

static VALUE
ary_shared_first(int argc, VALUE *argv, VALUE ary, bool last, bool remove)
{
    VALUE nv;
    rb_scan_args(argc, argv, "1", &nv);
    long n = NUM2LONG(nv);

    const long ary_len = RARY(ary)->len;
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
    rary_reserve(result, n);
    GC_MEMMOVE(rary_ptr(result),
	       &RARY(ary)->elements[RARY(ary)->beg + offset],
	       sizeof(VALUE) * n);
    RARY(result)->len = n;
    if (remove) {
	for (long i = 0; i < n; i++) {
	    rary_erase(ary, offset, 1);
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

VALUE
rary_push_m(VALUE ary, SEL sel, VALUE item)
{
    rary_modify(ary);
    rary_push(ary, item);
    return ary;
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
rary_push_m2(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    rary_modify(ary);
    while (argc-- > 0) {
	rary_push(ary, *argv++);
    }
    return ary;
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

VALUE
rary_pop(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    rary_modify(ary);
    if (argc == 0) {
	const long n = RARY(ary)->len;
	if (n == 0) {
	    return Qnil;
	}
	return rary_erase(ary, n - 1, 1);
    }
    return ary_shared_first(argc, argv, ary, true, true);
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

VALUE
rary_shift(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    rary_modify(ary);
    if (argc == 0) {
	if (RARY(ary)->len == 0) {
	    return Qnil;
	}
	return rary_erase(ary, 0, 1);
    }
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

VALUE
rary_unshift(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    rary_modify(ary);
    for (int i = argc - 1; i >= 0; i--) {
	rary_insert(ary, 0, argv[i]);
    }
    return ary;
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
rary_subseq(VALUE ary, long beg, long len)
{
    if (beg < 0 || len < 0) {
	return Qnil;
    }
    const long n = RARY(ary)->len;
    if (beg > n) {
	return Qnil;
    }
    if (n < len || n < beg + len) {
	len = n - beg;
    }
    VALUE newary = rary_alloc(rb_obj_class(ary), 0);
    if (len > 0) {
	rary_concat(newary, ary, beg, len);
    }	
    return newary;
}

VALUE
rary_aref(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    long beg, len;

    if (argc == 2) {
	beg = NUM2LONG(argv[0]);
	len = NUM2LONG(argv[1]);
	if (beg < 0) {
	    beg += RARRAY_LEN(ary);
	}
	return rary_subseq(ary, beg, len);
    }
    if (argc != 1) {
	rb_scan_args(argc, argv, "11", 0, 0);
    }
    VALUE arg = argv[0];
    /* special case - speeding up */
    if (FIXNUM_P(arg)) {
	return rary_entry(ary, FIX2LONG(arg));
    }
    /* check if idx is Range */
    switch (rb_range_beg_len(arg, &beg, &len, RARRAY_LEN(ary), 0)) {
	case Qfalse:
	    break;
	case Qnil:
	    return Qnil;
	default:
	    return rary_subseq(ary, beg, len);
    }
    return rary_entry(ary, NUM2LONG(arg));
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
rary_at(VALUE ary, SEL sel, VALUE pos)
{
    return rary_entry(ary, NUM2LONG(pos));
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
rary_first(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {
	if (RARY(ary)->len == 0) {
	    return Qnil;
	}
	return rary_elt(ary, 0);
    }
    return ary_shared_first(argc, argv, ary, false, false);
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
rary_last(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {
	const long n = RARY(ary)->len;
	if (n == 0) {
	    return Qnil;
	}
	return rary_elt(ary, n - 1);
    }
    return ary_shared_first(argc, argv, ary, true, false);
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
rary_fetch(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE pos, ifnone;

    rb_scan_args(argc, argv, "11", &pos, &ifnone);
    const bool block_given = rb_block_given_p();
    if (block_given && argc == 2) {
	rb_warn("block supersedes default value argument");
    }

    long idx = NUM2LONG(pos);
    if (idx < 0) {
	idx += RARY(ary)->len;
    }
    if (idx < 0 || RARY(ary)->len <= idx) {
	if (block_given) {
	    return rb_yield(pos);
	}
	if (argc == 1) {
	    rb_raise(rb_eIndexError, "index %ld out of array", idx);
	}
	return ifnone;
    }
    return rary_elt(ary, idx);
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

#define NOT_FOUND LONG_MAX

static size_t
rary_index_of_item(VALUE ary, size_t origin, VALUE item)
{
    assert(RARY(ary)->len == 0 || origin < RARY(ary)->len);
    for (size_t i = origin; i < RARY(ary)->len; i++) {
	VALUE item2 = rary_elt(ary, i);
	if (rb_equal_fast(item2, item) == Qtrue) {
	    return i;
	}
    }
    return NOT_FOUND;
}

static VALUE
rary_index(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE val;
    if (rb_scan_args(argc, argv, "01", &val) == 0) {
	RETURN_ENUMERATOR(ary, 0, 0);
	for (long i = 0; i < RARY(ary)->len; i++) {
	    VALUE elem = rary_elt(ary, i);
	    VALUE test = rb_yield(elem);
	    RETURN_IF_BROKEN();
	    if (RTEST(test)) {
		return LONG2NUM(i);
	    }
	}
    }
    else if (RARY(ary)->len > 0) {
	size_t pos = rary_index_of_item(ary, 0, val);
	if (pos != NOT_FOUND) {
	    return LONG2NUM(pos);
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

static size_t
rary_rindex_of_item(VALUE ary, long origin, VALUE item)
{
    assert(RARY(ary)->len == 0 || origin < RARY(ary)->len);
    for (long i = origin; i >= 0; i--) {
	VALUE item2 = rary_elt(ary, i);
	if (rb_equal_fast(item, item2) == Qtrue) {
	    return i;
	}
    }
    return NOT_FOUND;
}

static VALUE
rary_rindex(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {
	RETURN_ENUMERATOR(ary, 0, 0);
	long i = RARY(ary)->len;
	while (i-- > 0) {
	    VALUE elem = rary_elt(ary, i);
	    VALUE test = rb_yield(elem);
	    RETURN_IF_BROKEN();
	    if (RTEST(test)) {
		return LONG2NUM(i);
	    }
	    if (i > RARY(ary)->len) {
		i = RARY(ary)->len;
	    }
	}
    }
    else {
	VALUE val;
 	rb_scan_args(argc, argv, "01", &val);

	if (RARY(ary)->len > 0) {
	    size_t pos = rary_rindex_of_item(ary, RARY(ary)->len - 1, val);
	    if (pos != NOT_FOUND) {
		return LONG2NUM(pos);
	    }
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
    return rb_ary_new4(1, &obj);
}

static void
rary_splice(VALUE ary, long beg, long len, VALUE rpl)
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

    rary_modify(ary);

    if (ary == rpl) {
	rpl = rb_ary_dup(rpl);
    }
    if (beg >= n) {
	if (beg > ARY_MAX_SIZE - rlen) {
	    rb_raise(rb_eIndexError, "index %ld too big", beg);
	}
	for (long i = n; i < beg; i++) {
	    rary_push(ary, Qnil);
	}
	if (rlen > 0) {
	    rary_concat(ary, rpl, 0, rlen);
	}
    }
    else if (len == rlen) {
	if (rlen > 0 && IS_RARY(rpl)) {
	    GC_MEMMOVE(&RARY(ary)->elements[RARY(ary)->beg + beg],
		       &RARY(rpl)->elements[RARY(rpl)->beg],
		       sizeof(VALUE) * len);
	}
	else {
	    for (long i = 0; i < len; i++) {
		rary_elt_set(ary, beg + i, rb_ary_elt(rpl, i));
	    }
	}
    }
    else {
	if (rlen > 0 && IS_RARY(rpl)) {
	    long newlen = RARY(ary)->len + rlen - len;

	    rary_reserve(ary, newlen);
	    GC_MEMMOVE(&RARY(ary)->elements[RARY(ary)->beg + beg + rlen],
		       &RARY(ary)->elements[RARY(ary)->beg + beg + len],
		       sizeof(VALUE) * (RARY(ary)->len - (beg + len)));

	    GC_MEMMOVE(&RARY(ary)->elements[RARY(ary)->beg + beg],
		       rary_ptr(rpl),
		       sizeof(VALUE) * rlen);
	    RARY(ary)->len = newlen;
	}
	else {
	    rary_erase(ary, beg, len);
	    for (long i = 0; i < rlen; i++) {
		rary_insert(ary, beg + i, rb_ary_elt(rpl, i));
	    }
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
rary_aset(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    long offset, beg, len;

    if (argc == 3) {
	rary_modify(ary);
	rary_splice(ary, NUM2LONG(argv[0]), NUM2LONG(argv[1]), argv[2]);
	return argv[2];
    }
    if (argc != 2) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 2)", argc);
    }
    rary_modify(ary);
    if (FIXNUM_P(argv[0])) {
	offset = FIX2LONG(argv[0]);
    }
    else {
	if (rb_range_beg_len(argv[0], &beg, &len, RARRAY_LEN(ary), 1)) {
	    // Check if Range.
	    rary_splice(ary, beg, len, argv[1]);
	    return argv[1];
	}
	offset = NUM2LONG(argv[0]);
    }
    rary_store(ary, offset, argv[1]);
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
rary_insert_m(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    if (argc < 1) {
	rb_raise(rb_eArgError, "wrong number of arguments (at least 1)");
    }
    rary_modify(ary);
    if (argc == 1) {
	return ary;
    }
    long pos = NUM2LONG(argv[0]);
    if (pos == -1) {
	pos = RARRAY_LEN(ary);
    }
    if (pos < 0) {
	pos++;
    }
    if (argc == 2) {
	rary_insert(ary, pos, argv[1]);
    }
    else {
	rary_splice(ary, pos, 0, rb_ary_new4(argc - 1, argv + 1));
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
rary_each(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);
    for (long i = 0; i < RARY(ary)->len; i++) {
	VALUE elem = rary_elt(ary, i);
	rb_yield(elem);
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
rary_each_index(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);
    for (long i = 0; i < RARY(ary)->len; i++) {
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
rary_reverse_each(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);

    long len = RARY(ary)->len;
    while (len-- > 0) {
	VALUE elem = rary_elt(ary, len);
	rb_yield(elem);
	RETURN_IF_BROKEN();
	if (len > RARY(ary)->len) {
	    len = RARY(ary)->len;
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
rary_length(VALUE ary, SEL sel)
{
    return LONG2NUM(RARY(ary)->len);
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
rary_empty(VALUE ary, SEL sel)
{
    return RARY(ary)->len == 0 ? Qtrue : Qfalse;
}

static VALUE
rary_copy(VALUE rcv, VALUE klass)
{
    VALUE dup = rary_alloc(klass, 0);
    rary_concat(dup, rcv, 0, RARY(rcv)->len);
    return dup;
}

VALUE
rary_dup(VALUE ary, SEL sel)
{
    VALUE klass = CLASS_OF(ary);
    while (RCLASS_SINGLETON(klass)) {
	klass = RCLASS_SUPER(klass);
    }
    assert(rb_klass_is_rary(klass));

    VALUE dup = rary_alloc(klass, 0);
    rb_obj_invoke_initialize_copy(dup, ary);

    OBJ_INFECT(dup, ary);
    return dup;
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

// Defined on NSArray.
VALUE
rary_join(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE sep;
    rb_scan_args(argc, argv, "01", &sep);
    if (NIL_P(sep)) {
	sep = rb_output_fs;
    }
    return rb_ary_join(ary, sep);
}

/*
 *  call-seq:
 *     array.to_s -> string
 *     array.inspect  -> string
 *
 *  Create a printable version of <i>array</i>.
 */

static VALUE
inspect_ary(VALUE ary, VALUE dummy, int recur)
{
    if (recur) {
	return rb_tainted_str_new2("[...]");
    }

    bool tainted = OBJ_TAINTED(ary);
    bool untrusted = OBJ_UNTRUSTED(ary);
    VALUE str = rb_str_buf_new2("[");
    for (long i = 0; i < RARRAY_LEN(ary); i++) {
	VALUE s = rb_inspect(RARRAY_AT(ary, i));
	if (OBJ_TAINTED(s)) {
	    tainted = true;
	}
	if (OBJ_UNTRUSTED(s)) {
	    untrusted = true;
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
    if (untrusted) {
	OBJ_UNTRUST(str);
    }
    return str;
}

static VALUE
rary_inspect(VALUE ary, SEL sel)
{
    if (RARRAY_LEN(ary) == 0) {
	return rb_usascii_str_new2("[]");
    }
    return rb_exec_recursive(inspect_ary, ary, 0);
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

VALUE
rary_reverse_bang(VALUE ary, SEL sel)
{
    rary_modify(ary);
    if (RARY(ary)->len > 1) {
	for (size_t i = 0; i < RARY(ary)->len / 2; i++) {
	    const size_t j = RARY(ary)->len - i - 1;
	    VALUE elem = rary_elt(ary, i);
	    rary_elt_set(ary, i, rary_elt(ary, j));
	    rary_elt_set(ary, j, elem);
	}
    }
    return ary;
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
rary_reverse(VALUE ary, SEL sel)
{
    VALUE dup = rary_dup(ary, 0);

    if (RARY(dup)->len > 1) {
	for (size_t i = 0; i < RARY(dup)->len / 2; i++) {
	    const size_t j = RARY(dup)->len - i - 1;
	    VALUE elem = rary_elt(dup, i);
	    rary_elt_set(dup, i, rary_elt(dup, j));
	    rary_elt_set(dup, j, elem);
	}
    }
    return dup;
}

static inline long
rotate_count(long cnt, long len)
{
    return (cnt < 0) ? (len - (~cnt % len) - 1) : (cnt % len);
}

static void
ary_reverse(VALUE ary, long pos1, long pos2)
{
    while (pos1 < pos2) {
	VALUE elem = rb_ary_elt(ary, pos1);
	rb_ary_elt_set(ary, pos1, rb_ary_elt(ary, pos2));
	rb_ary_elt_set(ary, pos2, elem);
	pos1++;
	pos2--;
    }
}

VALUE
ary_rotate(VALUE ary, long cnt)
{
    if (cnt != 0) {
	long len = rb_ary_len(ary);
	if (len > 0 && (cnt = rotate_count(cnt, len)) > 0) {
	    --len;
	    if (cnt < len) {
		ary_reverse(ary, cnt, len);
	    }
	    if (--cnt > 0) {
		ary_reverse(ary, 0, cnt);
	    }
	    if (len > 0) {
		ary_reverse(ary, 0, len);
	    }
	    return ary;
	}
    }

    return Qnil;
}
    
/*
 *  call-seq:
 *     array.rotate!([cnt = 1]) -> array
 *
 *  Rotates _self_ in place so that the element at +cnt+ comes first,
 *  and returns _self_.  If +cnt+ is negative then it rotates in
 *  counter direction.
 *
 *     a = [ "a", "b", "c", "d" ]
 *     a.rotate!        #=> ["b", "c", "d", "a"]
 *     a                #=> ["b", "c", "d", "a"]
 *     a.rotate!(2)     #=> ["d", "a", "b", "c"]
 *     a.rotate!(-3)    #=> ["a", "b", "c", "d"]
 */

VALUE
rary_rotate_bang(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    long cnt = 1;

    switch (argc) {
      case 1: cnt = NUM2LONG(argv[0]);
      case 0: break;
      default: rb_scan_args(argc, argv, "01", NULL);
    }

    rb_ary_modify(ary);
    ary_rotate(ary, cnt);
    return ary;
}

/*
 *  call-seq:
 *     array.rotate([n = 1]) -> an_array
 *
 *  Returns new array by rotating _self_, whose first element is the
 *  element at +cnt+ in _self_.  If +cnt+ is negative then it rotates
 *  in counter direction.
 *
 *     a = [ "a", "b", "c", "d" ]
 *     a.rotate         #=> ["b", "c", "d", "a"]
 *     a                #=> ["a", "b", "c", "d"]
 *     a.rotate(2)      #=> ["c", "d", "a", "b"]
 *     a.rotate(-3)     #=> ["b", "c", "d", "a"]
 */

VALUE
rary_rotate(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    VALUE rotated;
    long cnt = 1;

    switch (argc) {
      case 1: cnt = NUM2LONG(argv[0]);
      case 0: break;
      default: rb_scan_args(argc, argv, "01", NULL);
    }

    rotated = rb_ary_dup(ary);
    if (rb_ary_len(ary) > 0) {
	ary_rotate(rotated, cnt);
    }
    return rotated;
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

#if 0 // TODO
    /* FIXME optimize!!! */
    if (TYPE(a) == T_STRING) {
	if (TYPE(b) == T_STRING) {
	    return rb_str_cmp(a, b);
	}
    }
#endif

    VALUE retval = rb_objs_cmp(a, b);
    return rb_cmpint(retval, a, b);
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
sort_bang(VALUE ary, bool is_dup)
{
    if (RARY(ary)->len > 1) {
	if (rb_block_given_p()) {
	    VALUE tmp = is_dup ? ary : rb_ary_dup(ary);
	    VALUE break_val = 0;

	    qsort_r(rary_ptr(tmp), RARY(ary)->len, sizeof(VALUE), &break_val,
		    sort_1);

	    if (break_val != 0) {
		return break_val;
	    }
	    if (!is_dup) {
		rb_ary_replace(ary, tmp);
	    }
	}
	else {
	    qsort_r(rary_ptr(ary), RARY(ary)->len, sizeof(VALUE), NULL,
		    sort_2);
	}
    }
    return ary;
}

VALUE
rary_sort_bang(VALUE ary, SEL sel)
{
    rary_modify(ary);
    return sort_bang(ary, false);
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

VALUE
rary_sort(VALUE ary, SEL sel)
{
    ary = rary_dup(ary, 0);
    return sort_bang(ary, true);
}

static VALUE
sort_by_i(VALUE i)
{
    return rb_yield(i);
}

/*
 *  call-seq:
 *     array.sort_by! {| obj | block }    -> array
 *
 *  Sorts <i>array</i> in place using a set of keys generated by mapping the
 *  values in <i>array</i> through the given block.
 */

static VALUE
rary_sort_by_bang(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);
    rb_ary_modify(ary);
    VALUE sorted = rb_objc_block_call(ary, sel_registerName("sort_by"), 0, 0,
	    sort_by_i, 0);
    rb_ary_replace(ary, sorted);
    return ary;
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
collect_bang(VALUE source, VALUE dest)
{
    for (long i = 0; i < RARY(source)->len; i++) {
	VALUE elem = rary_elt(source, i);
	VALUE new_elem = rb_yield(elem);
	RETURN_IF_BROKEN();
	rary_store(dest, i, new_elem);
    }
    return dest;
}

static VALUE
rary_collect_bang(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);

    rary_modify(ary);
    return collect_bang(ary, ary);
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
rary_collect(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);

    VALUE result = rb_ary_new2(RARY(ary)->len);
    return collect_bang(ary, result);
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

VALUE
rb_get_values_at(VALUE obj, long olen, int argc, VALUE *argv, 
	VALUE (*func) (VALUE, long))
{
    VALUE result = rb_ary_new2(argc);
    for (long i = 0; i < argc; i++) {
	if (FIXNUM_P(argv[i])) {
	    rary_push(result, (*func)(obj, FIX2LONG(argv[i])));
	    continue;
	}
	// Check if Range.
	long beg, len;
	switch (rb_range_beg_len(argv[i], &beg, &len, olen, 0)) {
	    case Qfalse:
		break;
	    case Qnil:
		continue;
	    default:
		for (long j = 0; j < len; j++) {
		    rary_push(result, (*func)(obj, j+beg));
		}
		continue;
	}
	rary_push(result, (*func)(obj, NUM2LONG(argv[i])));
    }
    return result;
}

static VALUE
rary_values_at(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    return rb_get_values_at(ary, RARY(ary)->len, argc, argv, rary_entry);
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
rary_select(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);

    VALUE result = rb_ary_new2(RARY(ary)->len);
    for (long i = 0; i < RARY(ary)->len; i++) {
	VALUE elem = rary_elt(ary, i);
	VALUE test = rb_yield(elem);
	RETURN_IF_BROKEN();
	if (RTEST(test)) {
	    rary_push(result, elem);
	}
    }
    return result;
}

/*
 *  call-seq:
 *     array.select! {|item| block } -> an_array
 *
 *  Invokes the block passing in successive elements from
 *  <i>array</i>, deleting elements for which the block returns a
 *  false value.  but returns <code>nil</code> if no changes were
 *  made.  Also see <code>Array#keep_if</code>
 */

static VALUE
rary_select_bang(VALUE ary, SEL sel)
{
    long i1, i2;

    RETURN_ENUMERATOR(ary, 0, 0);
    rb_ary_modify(ary);
    for (i1 = i2 = 0; i1 < RARY(ary)->len; i1++) {
	VALUE v = rary_elt(ary, i1);
	VALUE test = RTEST(rb_yield(v));
	RETURN_IF_BROKEN();
	if (!RTEST(test)) {
	    continue;
	}
	if (i1 != i2) {
	    rb_ary_store(ary, i2, v);
	}
	i2++;
    }

    if (RARY(ary)->len == i2) {
	return Qnil;
    }
    if (i2 < RARY(ary)->len) {
	RARY(ary)->len = i2;
    }
    return ary;
}

/*
 *  call-seq:
 *     array.keep_if {|item| block } -> an_array
 *
 *  Deletes every element of <i>self</i> for which <i>block</i> evaluates
 *  to <code>false</code>.
 *
 *     a = %w{ a b c d e f }
 *     a.keep_if {|v| v =~ /[aeiou]/}   #=> ["a", "e"]
 */

static VALUE
rary_keep_if(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);
    rary_select_bang(ary, 0);
    return ary;
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

static bool
rary_delete_element(VALUE ary, VALUE item, bool use_equal, bool check_modify)
{
    VALUE *p = rary_ptr(ary);
    VALUE *t = p;
    VALUE *end = p + RARY(ary)->len;

    if (use_equal) {
	while (t < end) {
	    if (RTEST(rb_equal_fast(*t, item))) {
		if (check_modify) {
		    rary_modify(ary);
		    check_modify = false;
		}
		t++;
	    }
	    else {
		GC_WB(p, *t);
		p++;
		t++;
	    }
	}
    }
    else {
	while (t < end) {
	    if (*t == item) {
		if (check_modify) {
		    rary_modify(ary);
		    check_modify = false;
		}
		t++;
	    }
	    else {
		GC_WB(p, *t);
		p++;
		t++;
	    }
	}
    }

    const size_t n = p - rary_ptr(ary);
    if (RARY(ary)->len == n) {
	// Nothing changed.
	return false;
    }
    RARY(ary)->len = n;
    return true;
}

VALUE
rary_delete(VALUE ary, SEL sel, VALUE item)
{
    const bool changed = rary_delete_element(ary, item, true, true);
    if (!changed) {
	if (rb_block_given_p()) {
	    return rb_yield(item);
	}
	return Qnil;
    }
    return item;
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

VALUE
rary_delete_at(VALUE ary, SEL sel, VALUE pos)
{
    long index = NUM2LONG(pos);
    if (index < 0) {
	index += RARY(ary)->len;
	if (index < 0) {
	    return Qnil;
	}
    }
    if (index >= RARY(ary)->len) {
	return Qnil;
    }
    rary_modify(ary);
    return rary_erase(ary, index, 1);
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
rary_slice_bang(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    const long alen = RARY(ary)->len;
    long pos, len;

    rary_modify(ary);
    VALUE arg1, arg2;
    if (rb_scan_args(argc, argv, "11", &arg1, &arg2) == 2) {
	pos = NUM2LONG(arg1);
	len = NUM2LONG(arg2);
delete_pos_len:
	if (len < 0) {
	    return Qnil;
	}
	if (pos < 0) {
	    pos = alen + pos;
	    if (pos < 0) {
		return Qnil;
	    }
	}
	else if (alen < pos) {
	    return Qnil;
	}
	if (alen < len || alen < pos + len) {
	    len = alen - pos;
	}
	if (len == 0) {
	    return rb_ary_new2(0);
	}
	arg2 = rary_subseq(ary, pos, len);
	rary_splice(ary, pos, len, Qundef);
	return arg2;
    }

    if (!FIXNUM_P(arg1)) {
	switch (rb_range_beg_len(arg1, &pos, &len, alen, 0)) {
	    case Qtrue:
		// Valid range.
		goto delete_pos_len;
	    case Qnil:
		// Invalid range.
		return Qnil;
	    default:
		// Not a range.
		break;
	}
    }
    return rary_delete_at(ary, 0, arg1);
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
rary_reject_bang(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);

    rary_modify(ary);
    bool changed = false;
    for (long i = 0, n = RARY(ary)->len; i < n; i++) {
	VALUE elem = rary_elt(ary, i);
	VALUE test = rb_yield(elem);
	RETURN_IF_BROKEN();
	if (RTEST(test)) {
	    rary_erase(ary, i, 1);	
	    n--;
	    i--;
	    changed = true;
	}
    }
    return changed ? ary : Qnil;
}

/*
 *  call-seq:
 *     array.reject {|item| block }  -> an_array
 *  
 *  Returns a new array containing the items in _self_
 *  for which the block is not true.
 */

static VALUE
rary_reject(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);
    ary = rary_dup(ary, 0);
    rary_reject_bang(ary, 0);
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
rary_delete_if(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);
    rary_reject_bang(ary, 0);
    return ary;
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
    
    rb_objc_block_call(obj, selEach, 0, 0, (VALUE(*)(ANYARGS))take_i,
	    (VALUE)args);

    return result;
}

// Defined on NSArray.
VALUE
rary_zip(VALUE ary, SEL sel, int argc, VALUE *argv)
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

// Defined on NSArray.
VALUE
rary_transpose(VALUE ary, SEL sel)
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

static VALUE
rary_replace(VALUE rcv, SEL sel, VALUE other)
{
    rary_modify(rcv);
    other = to_ary(other);
    if (rcv != other) {
	rary_remove_all(RARY(rcv));
	rary_concat(rcv, other, 0, RARRAY_LEN(other));
    }
    return rcv;
}

/*
 *  call-seq:
 *     array.to_a     -> array
 *
 *  Returns _self_. If called on a subclass of Array, converts
 *  the receiver to an Array object.
 */

static VALUE
rary_to_a(VALUE ary, SEL sel)
{
    if (rb_obj_class(ary) != rb_cRubyArray) {
        VALUE dup = rb_ary_new();
        rary_replace(dup, 0, ary);
        return dup;
    }
    return ary;
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

VALUE
rary_clear(VALUE ary, SEL sel)
{
    rary_modify(ary);
    rary_remove_all(RARY(ary));
    return ary;
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

// Defined on NSArray.
VALUE
rary_fill(VALUE ary, SEL sel, int argc, VALUE *argv)
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
    if (beg >= ARY_MAX_SIZE || len > ARY_MAX_SIZE - beg) {
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
 *     array.concat(other_array)   ->  array
 *
 *  Appends the elements in other_array to _self_.
 *  
 *     [ "a", "b" ].concat( ["c", "d"] ) #=> [ "a", "b", "c", "d" ]
 */

VALUE
rary_concat_m(VALUE x, SEL sel, VALUE y)
{
    rary_modify(x);
    y = to_ary(y);
    rary_concat(x, y, 0, RARRAY_LEN(y));
    return x;
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
rary_plus(VALUE x, SEL sel, VALUE y)
{
    y = to_ary(y);
    VALUE z = rb_ary_new2(0);
    rary_reserve(z, RARY(x)->len + RARRAY_LEN(y));
    rary_concat_m(z, 0, x);
    rary_concat_m(z, 0, y);
    return z;
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
rary_times(VALUE ary, SEL sel, VALUE times)
{
    VALUE tmp = rb_check_string_type(times);
    if (!NIL_P(tmp)) {
	return rb_ary_join(ary, tmp);
    }

    const long len = NUM2LONG(times);
    if (len < 0) {
	rb_raise(rb_eArgError, "negative argument");
    }
    VALUE ary2 = rary_alloc(rb_obj_class(ary), 0);
    if (len > 0) {
	const long n = RARY(ary)->len;
	if (ARY_MAX_SIZE/len < n) {
	    rb_raise(rb_eArgError, "argument too big");
	}
	rary_reserve(ary2, n * len);
	for (long i = 0; i < len; i++) {
	    rary_concat(ary2, ary, 0, n);
	}
    }

    OBJ_INFECT(ary2, ary);
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

// Defined on NSArray.
VALUE
rary_assoc(VALUE ary, SEL sel, VALUE key)
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

// Defined on NSArray.
VALUE
rary_rassoc(VALUE ary, SEL sel, VALUE value)
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

static VALUE
recursive_equal(VALUE ary1, VALUE ary2, int recur)
{
    if (recur) {
	return Qtrue; // like Ruby 1.9...
    }
    if (IS_RARY(ary1) && IS_RARY(ary2)) {
	if (RARY(ary1)->len != RARY(ary2)->len) {
	    return Qfalse;
	}
	for (size_t i = 0; i < RARY(ary1)->len; i++) {
	    VALUE item1 = rary_elt(ary1, i);
	    VALUE item2 = rary_elt(ary2, i);

	    if ((FIXFLOAT_P(item1) && isnan(FIXFLOAT2DBL(item1)))
		    || (FIXFLOAT_P(item2) && isnan(FIXFLOAT2DBL(item2)))) {
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

VALUE
rb_ary_equal(VALUE ary1, VALUE ary2)
{
    return rb_exec_recursive(recursive_equal, ary1, ary2);
}

static VALUE
rary_equal(VALUE ary1, SEL sel, VALUE ary2)
{
    if (ary1 == ary2) {
	return Qtrue;
    }
    if (TYPE(ary2) != T_ARRAY) {
	if (!rb_vm_respond_to(ary2, selToAry, false)) {
	    return Qfalse;
	}
	return rb_equal(ary2, ary1);
    }
    return rb_ary_equal(ary1, ary2);
}

/*
 *  call-seq:
 *     array.eql?(other)  -> true or false
 *
 *  Returns <code>true</code> if _array_ and _other_ are the same object,
 *  or are both arrays with the same content.
 */

static VALUE
recursive_eql(VALUE ary1, VALUE ary2, int recur)
{
    if (recur) {
	return Qfalse;
    }
    for (long i = 0; i < RARY(ary1)->len; i++) {
	if (!rb_eql(rary_elt(ary1, i), rb_ary_elt(ary2, i))) {
	    return Qfalse;
	}
    }
    return Qtrue;
}

static VALUE
rary_eql(VALUE ary1, SEL sel, VALUE ary2)
{
    if (ary1 == ary2) {
	return Qtrue;
    }
    if (TYPE(ary2) != T_ARRAY) {
	return Qfalse;
    }
    if (RARY(ary1)->len != RARRAY_LEN(ary2)) {
	return Qfalse;
    }
    return rb_exec_recursive(recursive_eql, ary1, ary2);
}

static VALUE
recursive_eql_fast(VALUE ary1, VALUE ary2, int recur)
{
    if (recur) {
	return Qfalse;
    }
    for (long i = 0; i < RARY(ary1)->len; i++) {
	if (!rb_eql(rary_elt(ary1, i), rary_elt(ary2, i))) {
	    return Qfalse;
	}
    }
    return Qtrue;
}

bool
rary_eql_fast(rb_ary_t *ary1, rb_ary_t *ary2)
{
    if (ary1 == ary2) {
	return true;
    }
    if (ary1->len != ary2->len) {
	return false;
    }
    return rb_exec_recursive(recursive_eql_fast, (VALUE)ary1, (VALUE)ary2);
}

/*
 *  call-seq:
 *     ary.hash   -> fixnum
 *
 *  Compute a hash-code for this array. Two arrays with the same content
 *  will have the same hash code (and will compare using <code>eql?</code>).
 */

VALUE
rary_hash(VALUE ary, SEL sel)
{
    return LONG2FIX(rb_ary_hash(ary));
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

VALUE
rary_includes(VALUE ary, SEL sel, VALUE item)
{
    if (RARY(ary)->len == 0) {
	return Qfalse;
    }
    return rary_index_of_item(ary, 0, item) != NOT_FOUND
	? Qtrue : Qfalse;
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

// Defined on NSArray.
VALUE
rary_cmp(VALUE ary1, SEL sel, VALUE ary2)
{
    ary2 = rb_check_array_type(ary2);
    if (NIL_P(ary2)) {
	return Qnil;
    }
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

VALUE
rb_ary_make_hash(VALUE ary1, VALUE ary2)
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

VALUE
rb_ary_make_hash_by(VALUE ary)
{
    VALUE hash = rb_hash_new();
    for (long i = 0; i < RARRAY_LEN(ary); ++i) {
	VALUE v = rb_ary_elt(ary, i), k = rb_yield(v);
	if (rb_hash_lookup(hash, k) == Qnil) {
	    rb_hash_aset(hash, k, v);
	}
    }
    return hash;
}

// Defined on NSArray.
VALUE
rary_diff(VALUE ary1, SEL sel, VALUE ary2)
{
    ary2 = to_ary(ary2);
    const long ary1_len = RARRAY_LEN(ary1);
    const long ary2_len = RARRAY_LEN(ary2);

    VALUE ary3 = rb_ary_new();

    if (ary2_len == 0) {
	rb_ary_concat(ary3, ary1);	
	return ary3;
    }

    VALUE hash = rb_ary_make_hash(ary2, 0);
    for (long i = 0; i < ary1_len; i++) {
	VALUE v = RARRAY_AT(ary1, i);
	if (rb_hash_has_key(hash, v) == Qfalse) {
	    rb_ary_push(ary3, rb_ary_elt(ary1, i));
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

// Defined on NSArray.
VALUE
rary_and(VALUE ary1, SEL sel, VALUE ary2)
{
    ary2 = to_ary(ary2);
    VALUE ary3 = rb_ary_new2(RARRAY_LEN(ary1) < RARRAY_LEN(ary2) ?
	    RARRAY_LEN(ary1) : RARRAY_LEN(ary2));
    VALUE hash = rb_ary_make_hash(ary2, 0);
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

// Defined on NSArray.
VALUE
rary_or(VALUE ary1, SEL sel, VALUE ary2)
{
    ary2 = to_ary(ary2);
    VALUE ary3 = rb_ary_new2(RARRAY_LEN(ary1) + RARRAY_LEN(ary2));
    VALUE hash = rb_ary_make_hash(ary1, ary2);
    filter_diff(ary1, ary3, hash);
    filter_diff(ary2, ary3, hash);
    return ary3;
}

static int
push_value(st_data_t key, st_data_t val, st_data_t ary)
{
    rb_ary_push((VALUE)ary, (VALUE)val);
    return ST_CONTINUE;
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
rary_uniq_bang(VALUE ary, SEL sel)
{
    VALUE hash, v;
    long i, j;

    rary_modify(ary);
    if (RARRAY_LEN(ary) <= 1) {
        return Qnil;
    }
    if (rb_block_given_p()) {
	hash = rb_ary_make_hash_by(ary);
	if (RARRAY_LEN(ary) == RHASH_SIZE(hash)) {
	    return Qnil;
	}
	RARY(ary)->len = 0;
	st_foreach(RHASH_TBL(hash), push_value, ary);
	rary_resize(ary, RHASH_SIZE(hash));
    }
    else {
	hash = rb_ary_make_hash(ary, 0);
	if (RARRAY_LEN(ary) == RHASH_SIZE(hash)) {
	    return Qnil;
	}
	for (i=j=0; i<RARRAY_LEN(ary); i++) {
	    st_data_t vv = (st_data_t)(v = rary_elt(ary, i));
	    if (st_delete(RHASH_TBL(hash), &vv, 0)) {
		rary_store(ary, j++, v);
	    }
	}
	rary_resize(ary, j);
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
rary_uniq(VALUE ary, SEL sel)
{
    ary = rary_dup(ary, 0);
    rary_uniq_bang(ary, 0);
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
rary_compact_bang(VALUE ary, SEL sel)
{
    rary_modify(ary);
    return rary_delete_element(ary, Qnil, false, false) ? ary : Qnil;
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
rary_compact(VALUE ary, SEL sel)
{
    ary = rary_dup(ary, 0);
    rary_compact_bang(ary, 0);
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
rary_count(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    long n = 0;
 
    if (argc == 0) {
	if (!rb_block_given_p())
	    return LONG2NUM(RARY(ary)->len);

	for (long i = 0; i < RARY(ary)->len; i++) {
	    VALUE elem = rary_elt(ary, i);
	    VALUE test = rb_yield(elem); 
	    RETURN_IF_BROKEN();
	    if (RTEST(test)) {
		n++;
	    }
	}
    }
    else {
	VALUE obj;
	rb_scan_args(argc, argv, "1", &obj);
	if (rb_block_given_p()) {
	    rb_warn("given block not used");
	}

	size_t pos = 0;
	while ((pos = rary_index_of_item(ary, pos, obj))
		!= NOT_FOUND) {
	    n++;
	    if (++pos == RARY(ary)->len) {
		break;
	    }
	}
    }

    return LONG2NUM(n);
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
flatten(VALUE ary, int level, int *modified)
{
    long i = 0;
    VALUE stack, result, tmp, elt;
    st_table *memo;
    st_data_t id;

    stack = rb_ary_new();
    VALUE klass = rb_class_of(ary);
    if (!rb_klass_is_rary(klass)) {
	klass = rb_cRubyArray;
    }
    result = rary_alloc(klass, 0);
    rary_reserve(result, RARRAY_LEN(ary));
    memo = st_init_numtable();
    st_insert(memo, (st_data_t)ary, (st_data_t)Qtrue);
    *modified = 0;

    while (true) {
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

// Defined on NSArray.
VALUE
rary_flatten_bang(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    int mod = 0, level = -1;
    VALUE result, lv;

    rb_scan_args(argc, argv, "01", &lv);
    rb_ary_modify(ary);
    if (!NIL_P(lv)) {
	level = NUM2INT(lv);
    }
    if (level == 0) {
	return Qnil;
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

// Defined on NSArray.
VALUE
rary_flatten(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    int mod = 0, level = -1;
    VALUE result, lv;

    rb_scan_args(argc, argv, "01", &lv);
    if (!NIL_P(lv)) {
	level = NUM2INT(lv);
    }
    if (level == 0) {
	return rb_ary_dup(ary);
    }

    result = flatten(ary, level, &mod);
    if (OBJ_TAINTED(ary)) {
	OBJ_TAINT(result);
    }
    if (OBJ_UNTRUSTED(ary)) {
	OBJ_UNTRUST(result);
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
rary_shuffle_bang(VALUE ary, SEL sel)
{
    rary_modify(ary);

    long i = RARY(ary)->len;
    while (i > 0) {
	const long j = rb_genrand_real() * i;
	VALUE elem = rb_ary_elt(ary, --i);
	rary_store(ary, i, rb_ary_elt(ary, j));
	rary_store(ary, j, elem);
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
rary_shuffle(VALUE ary, SEL sel)
{
    ary = rary_dup(ary, 0);
    rary_shuffle_bang(ary, 0);
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

// Defined on NSArray.
VALUE
rary_sample(VALUE ary, SEL sel, int argc, VALUE *argv)
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
    if (n < 0) {
	rb_raise(rb_eArgError, "negative count");
    }
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
	    {
		VALUE elems[] = { RARRAY_AT(ary, i), RARRAY_AT(ary, j) };
		return rb_ary_new4(2, elems);
	    }

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
		VALUE elems[] = { RARRAY_AT(ary, i), RARRAY_AT(ary, j),
		    RARRAY_AT(ary, k) };
		return rb_ary_new4(3, elems);
	    }
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
	VALUE *elems = (VALUE *)malloc(sizeof(VALUE) * n);
	assert(elems != NULL);
	for (i = 0; i < n; i++) {
	    elems[i] = RARRAY_AT(ary, idx[i]);
	}
	result = rb_ary_new4(n, elems);
	free(elems);
    }
    else {
	VALUE *elems = (VALUE *)malloc(sizeof(VALUE) * n);
	assert(elems != NULL);
	for (i = 0; i < n; i++) {
	    j = (long)(rb_genrand_real() * (len - i)) + i;
	    nv = RARRAY_AT(ary, j);
	    elems[i] = nv;
	}
	result = rb_ary_new4(n, elems);
	free(elems);
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

// Defined on NSArray.
VALUE
rary_cycle(VALUE ary, SEL sel, int argc, VALUE *argv)
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

// Defined on NSArray.
VALUE
rary_permutation(VALUE ary, SEL sel, int argc, VALUE *argv)
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
	    VALUE elem = RARRAY_AT(ary, i);
	    rb_yield(rb_ary_new4(1, &elem));
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

// Defined on NSArray.
VALUE
rary_combination(VALUE ary, SEL sel, VALUE num)
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
	    VALUE elem = RARRAY_AT(ary, i);
	    rb_yield(rb_ary_new4(1, &elem));
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

// Defined on NSArray.
VALUE
rary_product(VALUE ary, SEL sel, int argc, VALUE *argv)
{
    int n = argc+1;    /* How many arrays we're operating on */
    VALUE *arrays = (VALUE *)alloca(n * sizeof(VALUE));; /* The arrays we're computing the product of */
    int *counters = (int *)alloca(n * sizeof(int)); /* The current position in each one */
    VALUE result = Qnil; /* The array we'll be returning */
    long i,j;
    long resultlen = 1;

    /* initialize the arrays of arrays */
    arrays[0] = ary;
    for (i = 1; i < n; i++) arrays[i] = to_ary(argv[i-1]);

    /* initialize the counters for the arrays */
    for (i = 0; i < n; i++) counters[i] = 0;

	/* Otherwise, allocate and fill in an array of results */
    if (rb_block_given_p()) {
	/* Make defensive copies of arrays; exit if any is empty */
	for (i = 0; i < n; i++) {
	    if (RARRAY_LEN(arrays[i]) == 0) {
		return ary;
	    }
	}
    }
    else {
	/* Compute the length of the result array; return [] if any is empty */
	for (i = 0; i < n; i++) {
	    long k = RARRAY_LEN(arrays[i]), l = resultlen;
	    if (k == 0) {
		return rb_ary_new2(0);
	    }
	    resultlen *= k;
	    if (resultlen < k || resultlen < l || resultlen / k != l) {
		rb_raise(rb_eRangeError, "too big to product");
	    }
	}
	result = rb_ary_new2(resultlen);
    }
    for (;;) {
	int m;
	/* fill in one subarray */
	VALUE subarray = rb_ary_new2(n);
	for (j = 0; j < n; j++) {
	    rb_ary_push(subarray, rb_ary_entry(arrays[j], counters[j]));
	}

	if (NIL_P(result)) {
	    rb_yield(subarray);
	    RETURN_IF_BROKEN();
	}
	else {
	    /* put it on the result array */
	    rb_ary_push(result, subarray);
	}

	/*
	 * Increment the last counter.  If it overflows, reset to 0
	 * and increment the one before it.
	 */
	m = n-1;
	counters[m]++;
	while (counters[m] == RARRAY_LEN(arrays[m])) {
	    counters[m] = 0;
	    if (--m < 0) {
		goto done;
	    }
	    counters[m]++;
	}
    }

  done:
    return NIL_P(result) ? ary : result;
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
rary_take(VALUE obj, SEL sel, VALUE n)
{
    const long len = NUM2LONG(n);
    if (len < 0) {
	rb_raise(rb_eArgError, "attempt to take negative size");
    }
    return rary_subseq(obj, 0, len);
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
rary_take_while(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);

    long i = 0;
    while (i < RARY(ary)->len) {
	VALUE elem = rary_elt(ary, i);
	VALUE test = rb_yield(elem);
	RETURN_IF_BROKEN();
	if (!RTEST(test)) {
	    break;
	}
	i++;
    }
    return rary_take(ary, 0, LONG2FIX(i));
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
rary_drop(VALUE ary, SEL sel, VALUE n)
{
    const long pos = NUM2LONG(n);
    if (pos < 0) {
	rb_raise(rb_eArgError, "attempt to drop negative size");
    }

    VALUE result = rary_subseq(ary, pos, RARY(ary)->len);
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
rary_drop_while(VALUE ary, SEL sel)
{
    RETURN_ENUMERATOR(ary, 0, 0);

    long i = 0;
    while (i < RARY(ary)->len) {
	VALUE elem = rary_elt(ary, i);
	VALUE test = rb_yield(elem);
	RETURN_IF_BROKEN();
	if (!RTEST(test)) {
	    break;
	}
	i++;
    }
    return rary_drop(ary, 0, LONG2FIX(i));
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
    return RB2OC(rary_elt((VALUE)rcv, idx));
}

/*
 *  call-seq:
 *     Array(arg)    => array
 *  
 *  Returns <i>arg</i> as an <code>Array</code>. First tries to call
 *  <i>arg</i><code>.to_ary</code>, then <i>arg</i><code>.to_a</code>.
 *     
 *     Array(1..5)   #=> [1, 2, 3, 4, 5]
 */

VALUE
rb_f_array(VALUE obj, SEL sel, VALUE arg)
{
    VALUE ary = rb_Array(arg);
    if (!IS_RARY(ary)) {
	ary = rary_copy(ary, rb_cRubyArray);	
    }
    return ary;
}

static void
imp_rary_insertObjectAtIndex(void *rcv, SEL sel, void *obj, CFIndex idx)
{
    rary_insert((VALUE)rcv, idx, OC2RB(obj));
}

static void
imp_rary_removeObjectAtIndex(void *rcv, SEL sel, CFIndex idx)
{
    rary_erase((VALUE)rcv, idx, 1);
}

static void
imp_rary_replaceObjectAtIndexWithObject(void *rcv, SEL sel, CFIndex idx, 
	void *obj)
{
    rary_store((VALUE)rcv, idx, OC2RB(obj));
}

static void
imp_rary_addObject(void *rcv, SEL sel, void *obj)
{
    rary_push((VALUE)rcv, OC2RB(obj));
}

bool
rb_objc_ary_is_pure(VALUE ary)
{
    VALUE k = *(VALUE *)ary;
    while (RCLASS_SINGLETON(k)) {
        k = RCLASS_SUPER(k);
    }
    if (k == rb_cRubyArray) {
	return true;
    }
    while (k != 0) {
	if (k == rb_cRubyArray) {
	    return false;
	}
	k = RCLASS_SUPER(k);
    }
    return true;
}

/* Arrays are ordered, integer-indexed collections of any object. 
 * Array indexing starts at 0, as in C or Java.  A negative index is 
 * assumed to be relative to the end of the array---that is, an index of -1 
 * indicates the last element of the array, -2 is the next to last 
 * element in the array, and so on. 
 */

void Init_NSArray(void);

void
Init_Array(void)
{
    Init_NSArray();

    rb_cRubyArray = rb_define_class("Array", rb_cNSMutableArray);
    rb_objc_install_NSObject_special_methods((Class)rb_cRubyArray);

    rb_objc_define_method(*(VALUE *)rb_cRubyArray, "new",
	    rb_class_new_instance_imp, -1);
    rb_objc_define_method(*(VALUE *)rb_cRubyArray, "alloc", rary_alloc, 0);
    rb_objc_define_method(*(VALUE *)rb_cRubyArray, "[]", rary_s_create, -1);
    rb_objc_define_method(*(VALUE *)rb_cRubyArray, "try_convert",
	    rary_s_try_convert, 1);
    rb_objc_define_method(rb_cRubyArray, "initialize", rary_initialize, -1);
    rb_objc_define_method(rb_cRubyArray, "initialize_copy", rary_replace, 1);
    rb_objc_define_method(rb_cRubyArray, "to_a", rary_to_a, 0);
    rb_objc_define_method(rb_cRubyArray, "dup", rary_dup, 0);
    rb_objc_define_method(rb_cRubyArray, "to_s", rary_inspect, 0);
    rb_objc_define_method(rb_cRubyArray, "inspect", rary_inspect, 0);
    rb_objc_define_method(rb_cRubyArray, "==", rary_equal, 1);
    rb_objc_define_method(rb_cRubyArray, "eql?", rary_eql, 1);
    rb_objc_define_method(rb_cRubyArray, "[]", rary_aref, -1);
    rb_objc_define_method(rb_cRubyArray, "[]=", rary_aset, -1);
    rb_objc_define_method(rb_cRubyArray, "at", rary_at, 1);
    rb_objc_define_method(rb_cRubyArray, "fetch", rary_fetch, -1);
    rb_objc_define_method(rb_cRubyArray, "first", rary_first, -1);
    rb_objc_define_method(rb_cRubyArray, "last", rary_last, -1);
    rb_objc_define_method(rb_cRubyArray, "concat", rary_concat_m, 1);
    rb_objc_define_method(rb_cRubyArray, "<<", rary_push_m, 1);
    rb_objc_define_method(rb_cRubyArray, "push", rary_push_m2, -1);
    rb_objc_define_method(rb_cRubyArray, "pop", rary_pop, -1);
    rb_objc_define_method(rb_cRubyArray, "shift", rary_shift, -1);
    rb_objc_define_method(rb_cRubyArray, "unshift", rary_unshift, -1);
    rb_objc_define_method(rb_cRubyArray, "insert", rary_insert_m, -1);
    rb_objc_define_method(rb_cRubyArray, "each", rary_each, 0);
    rb_objc_define_method(rb_cRubyArray, "each_index", rary_each_index, 0);
    rb_objc_define_method(rb_cRubyArray, "reverse_each", rary_reverse_each, 0);
    rb_objc_define_method(rb_cRubyArray, "length", rary_length, 0);
    rb_objc_define_method(rb_cRubyArray, "size", rary_length, 0);
    rb_objc_define_method(rb_cRubyArray, "empty?", rary_empty, 0);
    rb_objc_define_method(rb_cRubyArray, "find_index", rary_index, -1);
    rb_objc_define_method(rb_cRubyArray, "index", rary_index, -1);
    rb_objc_define_method(rb_cRubyArray, "rindex", rary_rindex, -1);
    rb_objc_define_method(rb_cRubyArray, "reverse", rary_reverse, 0);
    rb_objc_define_method(rb_cRubyArray, "reverse!", rary_reverse_bang, 0);
    rb_objc_define_method(rb_cRubyArray, "sort", rary_sort, 0);
    rb_objc_define_method(rb_cRubyArray, "sort!", rary_sort_bang, 0);
    rb_objc_define_method(rb_cRubyArray, "sort_by!", rary_sort_by_bang, 0);
    rb_objc_define_method(rb_cRubyArray, "collect", rary_collect, 0);
    rb_objc_define_method(rb_cRubyArray, "collect!", rary_collect_bang, 0);
    rb_objc_define_method(rb_cRubyArray, "map", rary_collect, 0);
    rb_objc_define_method(rb_cRubyArray, "map!", rary_collect_bang, 0);
    rb_objc_define_method(rb_cRubyArray, "select", rary_select, 0);
    rb_objc_define_method(rb_cRubyArray, "select!", rary_select_bang, 0);
    rb_objc_define_method(rb_cRubyArray, "keep_if", rary_keep_if, 0);
    rb_objc_define_method(rb_cRubyArray, "values_at", rary_values_at, -1);
    rb_objc_define_method(rb_cRubyArray, "delete", rary_delete, 1);
    rb_objc_define_method(rb_cRubyArray, "delete_at", rary_delete_at, 1);
    rb_objc_define_method(rb_cRubyArray, "delete_if", rary_delete_if, 0);
    rb_objc_define_method(rb_cRubyArray, "reject", rary_reject, 0);
    rb_objc_define_method(rb_cRubyArray, "reject!", rary_reject_bang, 0);
    rb_objc_define_method(rb_cRubyArray, "replace", rary_replace, 1);
    rb_objc_define_method(rb_cRubyArray, "clear", rary_clear, 0);
    rb_objc_define_method(rb_cRubyArray, "include?", rary_includes, 1);
    rb_objc_define_method(rb_cRubyArray, "slice", rary_aref, -1);
    rb_objc_define_method(rb_cRubyArray, "slice!", rary_slice_bang, -1);
    rb_objc_define_method(rb_cRubyArray, "+", rary_plus, 1);
    rb_objc_define_method(rb_cRubyArray, "*", rary_times, 1);
    rb_objc_define_method(rb_cRubyArray, "uniq", rary_uniq, 0);
    rb_objc_define_method(rb_cRubyArray, "uniq!", rary_uniq_bang, 0);
    rb_objc_define_method(rb_cRubyArray, "compact", rary_compact, 0);
    rb_objc_define_method(rb_cRubyArray, "compact!", rary_compact_bang, 0);
    rb_objc_define_method(rb_cRubyArray, "count", rary_count, -1);
    rb_objc_define_method(rb_cRubyArray, "shuffle!", rary_shuffle_bang, 0);
    rb_objc_define_method(rb_cRubyArray, "shuffle", rary_shuffle, 0);
    rb_objc_define_method(rb_cRubyArray, "take", rary_take, 1);
    rb_objc_define_method(rb_cRubyArray, "take_while", rary_take_while, 0);
    rb_objc_define_method(rb_cRubyArray, "drop", rary_drop, 1);
    rb_objc_define_method(rb_cRubyArray, "drop_while", rary_drop_while, 0);

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
