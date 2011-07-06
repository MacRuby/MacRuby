/*
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#include "macruby_internal.h"
#include "ruby/encoding.h"
#include "ruby/node.h"
#include "vm.h"
#include "id.h"
#include "array.h"

VALUE rb_cRange;
static SEL selUpto, selBeg, selEnd, selExcludeEnd, selInclude; 

#define RANGE_BEG(r) (RSTRUCT(r)->as.ary[0])
#define RANGE_END(r) (RSTRUCT(r)->as.ary[1])
#define RANGE_EXCL(r) (RSTRUCT(r)->as.ary[2])

#define EXCL(r) RTEST(RANGE_EXCL(r))
#define SET_EXCL(r,v) (RSTRUCT(r)->as.ary[2] = (v) ? Qtrue : Qfalse)

static VALUE
range_failed(void)
{
    rb_raise(rb_eArgError, "bad value for range");
    return Qnil;		/* dummy */
}

static VALUE
range_check(VALUE *args)
{
    return rb_objs_cmp(args[0], args[1]);
}

static void
range_init(VALUE range, VALUE beg, VALUE end, int exclude_end)
{
    VALUE args[2];

    args[0] = beg;
    args[1] = end;
    
    if (!FIXNUM_P(beg) || !FIXNUM_P(end)) {
	VALUE v;

	v = rb_rescue(range_check, (VALUE)args, range_failed, 0);
	if (NIL_P(v))
	    range_failed();
    }

    SET_EXCL(range, exclude_end);
    GC_WB(&RANGE_BEG(range), beg);
    GC_WB(&RANGE_END(range), end);
}

VALUE
rb_range_new(VALUE beg, VALUE end, int exclude_end)
{
    VALUE range = rb_obj_alloc(rb_cRange);

    range_init(range, beg, end, exclude_end);
    return range;
}

VALUE
rb_range_new2(VALUE beg, VALUE end, int exclude_end, int retain)
{
    VALUE range = rb_range_new(beg, end, exclude_end);
    if (retain) {
	GC_RETAIN(range);
    }
    return range;
}

/*
 *  call-seq:
 *     Range.new(start, end, exclusive=false)    => range
 *  
 *  Constructs a range using the given <i>start</i> and <i>end</i>. If the third
 *  parameter is omitted or is <code>false</code>, the <i>range</i> will include
 *  the end object; otherwise, it will be excluded.
 */

static VALUE
range_initialize(VALUE range, SEL sel ,int argc, VALUE *argv)
{
    VALUE beg, end, flags;
    
    rb_scan_args(argc, argv, "21", &beg, &end, &flags);
    /* Ranges are immutable, so that they should be initialized only once. */
    if (RANGE_EXCL(range) != Qnil) {
	rb_name_error(rb_intern("initialize"), "`initialize' called twice");
    }
    range_init(range, beg, end, RTEST(flags));
    return Qnil;
}


/*
 *  call-seq:
 *     rng.exclude_end?    => true or false
 *  
 *  Returns <code>true</code> if <i>rng</i> excludes its end value.
 */

static VALUE
range_exclude_end_p(VALUE range, SEL sel)
{
    return EXCL(range) ? Qtrue : Qfalse;
}


/*
 *  call-seq:
 *     rng == obj    => true or false
 *  
 *  Returns <code>true</code> only if <i>obj</i> is a Range, has equivalent
 *  beginning and end items (by comparing them with <code>==</code>), and has
 *  the same #exclude_end? setting as <i>rng</t>.
 *     
 *    (0..2) == (0..2)            #=> true
 *    (0..2) == Range.new(0,2)    #=> true
 *    (0..2) == (0...2)           #=> false
 *     
 */

static VALUE
range_eq(VALUE range, SEL sel, VALUE obj)
{
    if (range == obj) {
	return Qtrue;
    }
    if (!rb_obj_is_kind_of(obj, rb_cRange)) {
	return Qfalse;
    }

    if (!rb_equal(RANGE_BEG(range), RANGE_BEG(obj))) {
	return Qfalse;
    }
    if (!rb_equal(RANGE_END(range), RANGE_END(obj))) {
	return Qfalse;
    }

    if (EXCL(range) != EXCL(obj)) {
	return Qfalse;
    }

    return Qtrue;
}

static int
r_lt(VALUE a, VALUE b)
{
    VALUE r = rb_objs_cmp(a, b);

    if (NIL_P(r))
	return Qfalse;
    if (rb_cmpint(r, a, b) < 0)
	return Qtrue;
    return Qfalse;
}

static int
r_le(VALUE a, VALUE b)
{
    int c;
    VALUE r = rb_objs_cmp(a, b);

    if (NIL_P(r))
	return Qfalse;
    c = rb_cmpint(r, a, b);
    if (c == 0)
	return INT2FIX(0);
    if (c < 0)
	return Qtrue;
    return Qfalse;
}


/*
 *  call-seq:
 *     rng.eql?(obj)    => true or false
 *  
 *  Returns <code>true</code> only if <i>obj</i> is a Range, has equivalent
 *  beginning and end items (by comparing them with #eql?), and has the same
 *  #exclude_end? setting as <i>rng</i>.
 *     
 *    (0..2) == (0..2)            #=> true
 *    (0..2) == Range.new(0,2)    #=> true
 *    (0..2) == (0...2)           #=> false
 *     
 */

static VALUE
range_eql(VALUE range, SEL sel, VALUE obj)
{
    if (range == obj) {
	return Qtrue;
    }
    if (!rb_obj_is_kind_of(obj, rb_cRange)) {
	return Qfalse;
    }

    if (!rb_eql(RANGE_BEG(range), RANGE_BEG(obj))) {
	return Qfalse;
    }
    if (!rb_eql(RANGE_END(range), RANGE_END(obj))) {
	return Qfalse;
    }

    if (EXCL(range) != EXCL(obj)) {
	return Qfalse;
    }

    return Qtrue;
}

/*
 * call-seq:
 *   rng.hash    => fixnum
 *
 * Generate a hash value such that two ranges with the same start and
 * end points, and the same value for the "exclude end" flag, generate
 * the same hash value.
 */

static VALUE
range_hash(VALUE range, SEL sel)
{
    long hash = EXCL(range);
    VALUE v;

    v = rb_hash(RANGE_BEG(range));
    hash ^= v << 1;
    v = rb_hash(RANGE_END(range));
    hash ^= v << 9;
    hash ^= EXCL(range) << 24;

    return LONG2FIX(hash);
}

static VALUE
range_each_func(VALUE range, VALUE (*func) (VALUE, void *), void *arg)
{
    int c;
    VALUE b = RANGE_BEG(range);
    VALUE e = RANGE_END(range);
    VALUE v = b;

    if (EXCL(range)) {
	while (r_lt(v, e)) {
	    (*func) (v, arg);
	    RETURN_IF_BROKEN();
	    v = rb_vm_call(v, selSucc, 0, NULL);
	}
    }
    else {
	while (RTEST(c = r_le(v, e))) {
	    (*func) (v, arg);
	    RETURN_IF_BROKEN();
	    if (c == INT2FIX(0))
		break;
	    v = rb_vm_call(v, selSucc, 0, NULL);
	}
    }
    return range;
}

static VALUE
step_i(VALUE i, void *arg)
{
    VALUE *iter = arg;

    if (FIXNUM_P(iter[0])) {
	iter[0] -= INT2FIX(1) & ~FIXNUM_FLAG;
    }
    else {
	VALUE one = INT2FIX(1);
	iter[0] = rb_vm_call(iter[0], selMINUS, 1, &one);
    }
    if (iter[0] == INT2FIX(0)) {
	rb_yield(i);
	iter[0] = iter[1];
    }
    return Qnil;
}

static VALUE
sym_step_i(VALUE i, void *arg)
{
    VALUE *iter = arg;

    if (FIXNUM_P(iter[0])) {
	iter[0] -= INT2FIX(1) & ~FIXNUM_FLAG;
    }
    else {
	VALUE one = INT2FIX(1);
	iter[0] = rb_vm_call(iter[0], selMINUS,1, &one);
    }
    if (iter[0] == INT2FIX(0)) {
	rb_yield(ID2SYM(rb_intern_str(i)));
	iter[0] = iter[1];
    }
    return Qnil;
}

/*
 *  call-seq:
 *     rng.step(n=1) {| obj | block }    => rng
 *  
 *  Iterates over <i>rng</i>, passing each <i>n</i>th element to the block. If
 *  the range contains numbers, <i>n</i> is added for each iteration.  Otherwise
 *  <code>step</code> invokes <code>succ</code> to iterate through range
 *  elements. The following code uses class <code>Xs</code>, which is defined
 *  in the class-level documentation.
 *     
 *     range = Xs.new(1)..Xs.new(10)
 *     range.step(2) {|x| puts x}
 *     range.step(3) {|x| puts x}
 *     
 *  <em>produces:</em>
 *     
 *      1 x
 *      3 xxx
 *      5 xxxxx
 *      7 xxxxxxx
 *      9 xxxxxxxxx
 *      1 x
 *      4 xxxx
 *      7 xxxxxxx
 *     10 xxxxxxxxxx
 */


static VALUE
range_step(VALUE range, SEL sel, int argc, VALUE *argv)
{
    VALUE b, e, step, tmp;

    RETURN_ENUMERATOR(range, argc, argv);

    b = RANGE_BEG(range);
    e = RANGE_END(range);
    if (argc == 0) {
	step = INT2FIX(1);
    }
    else {
	rb_scan_args(argc, argv, "01", &step);
	if (!rb_obj_is_kind_of(step, rb_cNumeric)) {
	    step = rb_to_int(step);
	}
	VALUE zero = INT2FIX(0);
	if (rb_vm_call(step, selLT, 1, &zero)) {
	    rb_raise(rb_eArgError, "step can't be negative");
	}
	else if (!rb_vm_call(step, selGT, 1, &zero)) {
	    rb_raise(rb_eArgError, "step can't be 0");
	}
    }

    if (FIXNUM_P(b) && FIXNUM_P(e) && FIXNUM_P(step)) { /* fixnums are special */
	long end = FIX2LONG(e);
	long i, unit = FIX2LONG(step);

	if (!EXCL(range))
	    end += 1;
	i = FIX2LONG(b);	
	while (i < end) {
	    rb_yield(LONG2NUM(i));
	    RETURN_IF_BROKEN();
	    if (i + unit < i) break;
	    i += unit;
	}

    }
    else if (SYMBOL_P(b) && SYMBOL_P(e)) { /* symbols are special */
	VALUE args[2];
	VALUE iter[2];

	args[0] = rb_sym_to_s(e);
	args[1] = EXCL(range) ? Qtrue : Qfalse;
	iter[0] = INT2FIX(1);
	iter[1] = step;
	rb_objc_block_call(rb_sym_to_s(b), selUpto, 2, args, sym_step_i, (VALUE)iter);
    }
    else if (rb_obj_is_kind_of(b, rb_cNumeric) ||
	     !NIL_P(rb_check_to_integer(b, "to_int")) ||
	     !NIL_P(rb_check_to_integer(e, "to_int"))) {
	SEL op = EXCL(range) ? selLT : selLE;

	while (RTEST(rb_vm_call(b, op, 1, &e))) {
	    rb_yield(b);
	    RETURN_IF_BROKEN();
	    b = rb_vm_call(b, selPLUS, 1, &step);
	}
    }
    else {
	tmp = rb_check_string_type(b);

	if (!NIL_P(tmp)) {
	    VALUE args[2], iter[2];

	    b = tmp;
	    args[0] = e;
	    args[1] = EXCL(range) ? Qtrue : Qfalse;
	    iter[0] = INT2FIX(1);
	    iter[1] = step;
	    rb_objc_block_call(b, selUpto, 2, args, step_i, (VALUE)iter);
	}
	else {
	    VALUE args[2];

	    if (!rb_vm_respond_to(b, selSucc, true)) {
		rb_raise(rb_eTypeError, "can't iterate from %s",
			 rb_obj_classname(b));
	    }
	    args[0] = INT2FIX(1);
	    args[1] = step;
	    return range_each_func(range, step_i, args);
	}
    }
    return range;
}

static VALUE
each_i(VALUE v, void *arg)
{
    rb_yield(v);
    return Qnil;
}

static VALUE
sym_each_i(VALUE v, void *arg)
{
    rb_yield(ID2SYM(rb_intern_str(v)));
    return Qnil;
}

/*
 *  call-seq:
 *     rng.each {| i | block } => rng
 *  
 *  Iterates over the elements <i>rng</i>, passing each in turn to the
 *  block. You can only iterate if the start object of the range
 *  supports the +succ+ method (which means that you can't iterate over
 *  ranges of +Float+ objects).
 *     
 *     (10..15).each do |n|
 *        print n, ' '
 *     end
 *     
 *  <em>produces:</em>
 *     
 *     10 11 12 13 14 15
 */

static VALUE
range_each(VALUE range, SEL sel)
{
    VALUE beg, end;

    RETURN_ENUMERATOR(range, 0, 0);

    beg = RANGE_BEG(range);
    end = RANGE_END(range);

    if (!rb_vm_respond_to(beg, selSucc, false)) {
	rb_raise(rb_eTypeError, "can't iterate from %s",
		 rb_obj_classname(beg));
    }
    if (FIXNUM_P(beg) && FIXNUM_P(end)) { /* fixnums are special */
	long lim = FIX2LONG(end);
	long i;

	if (!EXCL(range))
	    lim += 1;
	for (i = FIX2LONG(beg); i < lim; i++) {
	    rb_yield(LONG2FIX(i));
	    RETURN_IF_BROKEN();
	}
    }
    else if (SYMBOL_P(beg) && SYMBOL_P(end)) { /* symbols are special */
	VALUE args[2];

	args[0] = rb_sym_to_s(end);
	args[1] = EXCL(range) ? Qtrue : Qfalse;
	rb_objc_block_call(rb_sym_to_s(beg), selUpto, 2, args, sym_each_i, 0);
    }
    else if (TYPE(beg) == T_STRING) {
	VALUE args[2];

	args[0] = end;
	args[1] = EXCL(range) ? Qtrue : Qfalse;
	rb_objc_block_call(beg, selUpto, 2, args, rb_yield, 0);
    }
    else {
	return range_each_func(range, each_i, NULL);
    }
    return range;
}

/*
 *  call-seq:
 *     rng.begin    => obj
 *  
 *  Returns the first object in <i>rng</i>.
 */

static VALUE
range_begin(VALUE range, SEL sel)
{
    return RANGE_BEG(range);
}


/*
 *  call-seq:
 *     rng.end    => obj
 *  
 *  Returns the object that defines the end of <i>rng</i>.
 *     
 *     (1..10).end    #=> 10
 *     (1...10).end   #=> 10
 */


static VALUE
range_end(VALUE range, SEL sel)
{
    return RANGE_END(range);
}


static VALUE
first_i(VALUE i, VALUE *ary)
{
    long n = NUM2LONG(ary[0]);

    if (n <= 0) {
	rb_iter_break();
    }
    rb_ary_push(ary[1], i);
    n--;
    ary[0] = INT2NUM(n);
    return Qnil;
}

/*
 *  call-seq:
 *     rng.first    => obj
 *     rng.first(n) => an_array
 *  
 *  Returns the first object in <i>rng</i>, or the first +n+ elements.
 */

static VALUE
range_first(VALUE range, SEL sel, int argc, VALUE *argv)
{
    VALUE n, ary[2];

    if (argc == 0) return RANGE_BEG(range);

    rb_scan_args(argc, argv, "1", &n);
    ary[0] = n;
    ary[1] = rb_ary_new2(NUM2LONG(n));
    rb_objc_block_call(range, selEach, 0, 0, first_i, (VALUE)ary);

    return ary[1];
}


/*
 *  call-seq:
 *     rng.last    => obj
 *     rng.last(n) => an_array
 *  
 *  Returns the last object in <i>rng</i>, or the last +n+ elements.
 */

static VALUE
range_last(VALUE range, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {
	return RANGE_END(range);
    }
    return rary_last(rb_Array(range), 0, argc, argv);
}


/*
 *  call-seq:
 *     rng.min                    => obj
 *     rng.min {| a,b | block }   => obj
 *  
 *  Returns the minimum value in <i>rng</i>. The second uses
 *  the block to compare values.  Returns nil if the first
 *  value in range is larger than the last value.
 *     
 */


static VALUE
range_min(VALUE range, SEL sel)
{
    if (rb_block_given_p()) {
	if (sel == NULL) {
	    sel = sel_registerName("min");
	}
	return rb_vm_call_super(range, sel, 0, NULL);
    }
    else {
	VALUE b = RANGE_BEG(range);
	VALUE e = RANGE_END(range);
	int c = rb_cmpint(rb_objs_cmp(b, e), b, e);

	if (c > 0 || (c == 0 && EXCL(range)))
	    return Qnil;
	return b;
    }
}

/*
 *  call-seq:
 *     rng.max                    => obj
 *     rng.max {| a,b | block }   => obj
 *  
 *  Returns the maximum value in <i>rng</i>. The second uses
 *  the block to compare values.  Returns nil if the first
 *  value in range is larger than the last value.
 *     
 */


static VALUE
range_max(VALUE range, SEL sel)
{
    VALUE e = RANGE_END(range);
    int nm = FIXNUM_P(e) || rb_obj_is_kind_of(e, rb_cNumeric);

    if (rb_block_given_p() || (EXCL(range) && !nm)) {
	if (sel == NULL) {
	    sel = sel_registerName("max");
	}
	return rb_vm_call_super(range, sel, 0, NULL);
    }
    else {
	VALUE b = RANGE_BEG(range);
	int c = rb_cmpint(rb_objs_cmp(b, e), b, e);

	if (c > 0)
	    return Qnil;
	if (EXCL(range)) {
	    if (!FIXNUM_P(e) && !rb_obj_is_kind_of(e, rb_cInteger)) {
		rb_raise(rb_eTypeError, "cannot exclude non Integer end value");
	    }
	    if (c == 0) {
		return Qnil;
	    }
	    if (FIXNUM_P(e)) {
		return LONG2NUM(FIX2LONG(e) - 1);
	    }
	    VALUE one = INT2FIX(1);
	    return rb_vm_call(e, selMINUS, 1, &one);
	}
	return e;
    }
}

void
rb_range_extract(VALUE range, VALUE *begp, VALUE *endp, bool *exclude)
{
    assert(begp != NULL && endp != NULL && exclude != NULL);
    *begp = RANGE_BEG(range);
    *endp = RANGE_END(range);
    *exclude = EXCL(range);
}

int
rb_range_values(VALUE range, VALUE *begp, VALUE *endp, int *exclp)
{
    VALUE b, e;
    int excl;

    if (rb_obj_is_kind_of(range, rb_cRange)) {
        b = RANGE_BEG(range);
        e = RANGE_END(range);
        excl = EXCL(range);
    }
    else {
	if (!rb_vm_respond_to(range, selBeg, false)) {
	    return 0;
	}
	if (!rb_vm_respond_to(range, selEnd, false)) {
	    return 0;
	}
	b = rb_vm_call(range, selBeg, 0, NULL);
	e = rb_vm_call(range, selEnd, 0, NULL);
	excl = RTEST(rb_vm_call(range, selExcludeEnd, 0, NULL));
    }
    *begp = b;
    *endp = e;
    *exclp = excl;
    return 1;
}

VALUE
rb_range_beg_len(VALUE range, long *begp, long *lenp, long len, int err)
{
    VALUE b, e;
    long beg, end, excl;

    if (rb_obj_is_kind_of(range, rb_cRange)) {
	b = RANGE_BEG(range);
	e = RANGE_END(range);
	excl = EXCL(range);
    }
    else {
	if (!rb_vm_respond_to(range, selBeg, false)) {
	    return Qfalse;
	}
	if (!rb_vm_respond_to(range, selEnd, false)) {
	    return Qfalse;
	}
	b = rb_vm_call(range, selBeg, 0, NULL);
	e = rb_vm_call(range, selEnd, 0, NULL);
	excl = RTEST(rb_vm_call(range, selExcludeEnd, 0, NULL));
    }
    beg = NUM2LONG(b);
    end = NUM2LONG(e);

    if (beg < 0) {
	beg += len;
	if (beg < 0)
	    goto out_of_range;
    }
    if (err == 0 || err == 2) {
	if (beg > len)
	    goto out_of_range;
	if (end > len)
	    end = len;
    }
    if (end < 0)
	end += len;
    if (!excl)
	end++;			/* include end point */
    len = end - beg;
    if (len < 0)
	len = 0;

    *begp = beg;
    *lenp = len;
    return Qtrue;

  out_of_range:
    if (err) {
	rb_raise(rb_eRangeError, "%ld..%s%ld out of range",
		 b, excl ? "." : "", e);
    }
    return Qnil;
}

/*
 * call-seq:
 *   rng.to_s   => string
 *
 * Convert this range object to a printable form.
 */

static VALUE
range_to_s(VALUE range, SEL sel)
{
    VALUE str, str2;

    str = rb_obj_as_string(RANGE_BEG(range));
    str2 = rb_obj_as_string(RANGE_END(range));
    str = rb_str_dup(str);
    rb_str_cat(str, "...", EXCL(range) ? 3 : 2);
    rb_str_append(str, str2);
    OBJ_INFECT(str, str2);

    return str;
}

/*
 * call-seq:
 *   rng.inspect  => string
 *
 * Convert this range object to a printable form (using 
 * <code>inspect</code> to convert the start and end
 * objects).
 */


static VALUE
range_inspect(VALUE range, SEL sel)
{
    VALUE str, str2;

    str = rb_inspect(RANGE_BEG(range));
    str2 = rb_inspect(RANGE_END(range));
    str = rb_str_dup(str);
    rb_str_cat(str, "...", EXCL(range) ? 3 : 2);
    rb_str_append(str, str2);
    OBJ_INFECT(str, str2);

    return str;
}

/*
 *  call-seq:
 *     rng === obj       =>  true or false
 *  
 *  Returns <code>true</code> if <i>obj</i> is an element of
 *  <i>rng</i>, <code>false</code> otherwise. Conveniently,
 *  <code>===</code> is the comparison operator used by
 *  <code>case</code> statements.
 *     
 *     case 79
 *     when 1..50   then   print "low\n"
 *     when 51..75  then   print "medium\n"
 *     when 76..100 then   print "high\n"
 *     end
 *     
 *  <em>produces:</em>
 *     
 *     high
 */

static VALUE
range_eqq(VALUE range, SEL sel, VALUE val)
{
    return rb_vm_call(range, selInclude, 1, &val);
}

/*
 *  call-seq:
 *     rng.member?(val)  =>  true or false
 *     rng.include?(val) =>  true or false
 *  
 *  Returns <code>true</code> if <i>obj</i> is an element of
 *  <i>rng</i>, <code>false</code> otherwise.  If beg and end are
 *  numeric, comparison is done according magnitude of values.
 *     
 *     ("a".."z").include?("g")  # => true
 *     ("a".."z").include?("A")  # => false
 */

static VALUE
range_include(VALUE range, SEL sel, VALUE val)
{
    VALUE beg = RANGE_BEG(range);
    VALUE end = RANGE_END(range);
    int nv = FIXNUM_P(beg) || FIXNUM_P(end) ||
	     rb_obj_is_kind_of(beg, rb_cNumeric) ||
	     rb_obj_is_kind_of(end, rb_cNumeric);

    if (nv ||
	!NIL_P(rb_check_to_integer(beg, "to_int")) ||
	!NIL_P(rb_check_to_integer(end, "to_int"))) {
	if (r_le(beg, val)) {
	    if (EXCL(range)) {
		if (r_lt(val, end))
		    return Qtrue;
	    }
	    else {
		if (r_le(val, end))
		    return Qtrue;
	    }
	}
	return Qfalse;
    }
    else if (TYPE(beg) == T_STRING && TYPE(end) == T_STRING &&
	     RSTRING_LEN(beg) == 1 && RSTRING_LEN(end) == 1) {
	if (NIL_P(val)) return Qfalse;
	if (TYPE(val) == T_STRING) {
	    if (RSTRING_LEN(val) == 0 || RSTRING_LEN(val) > 1)
		return Qfalse;
	    else {
		char b = RSTRING_PTR(beg)[0];
		char e = RSTRING_PTR(end)[0];
		char v = RSTRING_PTR(val)[0];

		if (ISASCII(b) && ISASCII(e) && ISASCII(v)) {
		    if (b <= v && v < e) return Qtrue;
		    if (!EXCL(range) && v == e) return Qtrue;
		    return Qfalse;
		}
	    }
	}
    }
    if (sel == NULL) {
	sel = sel_registerName("include?:");
    }
    return rb_vm_call_super(range, sel, 1, &val);
}


/*
 *  call-seq:
 *     rng.cover?(val)  =>  true or false
 *  
 *  Returns <code>true</code> if <i>obj</i> is between beg and end,
 *  i.e <code>beg <= obj <= end</code> (or <i>end</i> exclusive when
 *  <code>exclude_end?</code> is true).
 *     
 *     ("a".."z").cover?("c")    #=> true
 *     ("a".."z").cover?("5")    #=> false
 */

static VALUE
range_cover(VALUE range, SEL sel, VALUE val)
{
    VALUE beg, end;

    beg = RANGE_BEG(range);
    end = RANGE_END(range);
    if (r_le(beg, val)) {
	if (EXCL(range)) {
	    if (r_lt(val, end))
		return Qtrue;
	}
	else {
	    if (r_le(val, end))
		return Qtrue;
	}
    }
    return Qfalse;
}

#if 0
static VALUE
range_dumper(VALUE range)
{
    VALUE v;
    NEWOBJ(m, struct RObject);
    OBJSETUP(m, rb_cObject, T_OBJECT);

    v = (VALUE)m;

    rb_ivar_set(v, id_excl, RANGE_EXCL(range));
    rb_ivar_set(v, id_beg, RANGE_BEG(range));
    rb_ivar_set(v, id_end, RANGE_END(range));
    return v;
}

static VALUE
range_loader(VALUE range, VALUE obj)
{
    if (TYPE(obj) != T_OBJECT || RBASIC(obj)->klass != rb_cObject) {
        rb_raise(rb_eTypeError, "not a dumped range object");
    }

    RSTRUCT(range)->as.ary[0] = rb_ivar_get(obj, id_beg);
    RSTRUCT(range)->as.ary[1] = rb_ivar_get(obj, id_end);
    RSTRUCT(range)->as.ary[2] = rb_ivar_get(obj, id_excl);
    return range;
}
#endif

static VALUE
range_alloc(VALUE klass)
{
  /* rb_struct_alloc_noinit itself should not be used because
   * rb_marshal_define_compat uses equality of allocaiton function */
    return rb_struct_alloc_noinit(klass);
}

/*  A <code>Range</code> represents an interval---a set of values with a
 *  start and an end. Ranges may be constructed using the
 *  <em>s</em><code>..</code><em>e</em> and
 *  <em>s</em><code>...</code><em>e</em> literals, or with
 *  <code>Range::new</code>. Ranges constructed using <code>..</code>
 *  run from the start to the end inclusively. Those created using
 *  <code>...</code> exclude the end value. When used as an iterator,
 *  ranges return each value in the sequence.
 *     
 *     (-1..-5).to_a      #=> []
 *     (-5..-1).to_a      #=> [-5, -4, -3, -2, -1]
 *     ('a'..'e').to_a    #=> ["a", "b", "c", "d", "e"]
 *     ('a'...'e').to_a   #=> ["a", "b", "c", "d"]
 *     
 *  Ranges can be constructed using objects of any type, as long as the
 *  objects can be compared using their <code><=></code> operator and
 *  they support the <code>succ</code> method to return the next object
 *  in sequence.
 *     
 *     class Xs                # represent a string of 'x's
 *       include Comparable
 *       attr :length
 *       def initialize(n)
 *         @length = n
 *       end
 *       def succ
 *         Xs.new(@length + 1)
 *       end
 *       def <=>(other)
 *         @length <=> other.length
 *       end
 *       def to_s
 *         sprintf "%2d #{inspect}", @length
 *       end
 *       def inspect
 *         'x' * @length
 *       end
 *     end
 *     
 *     r = Xs.new(3)..Xs.new(6)   #=> xxx..xxxxxx
 *     r.to_a                     #=> [xxx, xxxx, xxxxx, xxxxxx]
 *     r.member?(Xs.new(5))       #=> true
 *     
 *  In the previous code example, class <code>Xs</code> includes the
 *  <code>Comparable</code> module. This is because
 *  <code>Enumerable#member?</code> checks for equality using
 *  <code>==</code>. Including <code>Comparable</code> ensures that the
 *  <code>==</code> method is defined in terms of the <code><=></code>
 *  method implemented in <code>Xs</code>.
 *     
 */

void
Init_Range(void)
{
    rb_cRange = rb_struct_define_without_accessor(
        "Range", rb_cObject, range_alloc,
        "begin", "end", "excl", NULL);

    rb_include_module(rb_cRange, rb_mEnumerable);
    //rb_marshal_define_compat(rb_cRange, rb_cObject, range_dumper, range_loader);
    rb_objc_define_method(rb_cRange, "initialize", range_initialize, -1);
    rb_objc_define_method(rb_cRange, "==", range_eq, 1);
    rb_objc_define_method(rb_cRange, "===", range_eqq, 1);
    rb_objc_define_method(rb_cRange, "eql?", range_eql, 1);
    rb_objc_define_method(rb_cRange, "hash", range_hash, 0);
    rb_objc_define_method(rb_cRange, "each", range_each, 0);
    rb_objc_define_method(rb_cRange, "step", range_step, -1);
    rb_objc_define_method(rb_cRange, "begin", range_begin, 0);
    rb_objc_define_method(rb_cRange, "end", range_end, 0);
    rb_objc_define_method(rb_cRange, "first", range_first, -1);
    rb_objc_define_method(rb_cRange, "last", range_last, -1);
    rb_objc_define_method(rb_cRange, "min", range_min, 0);
    rb_objc_define_method(rb_cRange, "max", range_max, 0);
    rb_objc_define_method(rb_cRange, "to_s", range_to_s, 0);
    rb_objc_define_method(rb_cRange, "inspect", range_inspect, 0);

    rb_objc_define_method(rb_cRange, "exclude_end?", range_exclude_end_p, 0);

    rb_objc_define_method(rb_cRange, "member?", range_include, 1);
    rb_objc_define_method(rb_cRange, "include?", range_include, 1);
    rb_objc_define_method(rb_cRange, "cover?", range_cover, 1);

    selUpto = sel_registerName("upto:");
    selBeg = sel_registerName("begin");
    selEnd = sel_registerName("end");
    selExcludeEnd = sel_registerName("exclude_end?");
    selInclude = sel_registerName("include?:");
}
