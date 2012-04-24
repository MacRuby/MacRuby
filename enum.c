/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#include "macruby_internal.h"
#include "ruby/node.h"
#include "ruby/util.h"
#include "vm.h"
#include "id.h"
#include "array.h"

VALUE rb_mEnumerable;
static ID id_each, id_eqq, id_next, id_size;

static VALUE
enum_values_pack(int argc, VALUE *argv)
{
    if (argc == 0) return Qnil;
    if (argc == 1) return argv[0];
    return rb_ary_new4(argc, argv);
}

#define ENUM_WANT_SVALUE() do { \
    i = enum_values_pack(argc, argv); \
} while (0)

#define enum_yield rb_yield_values2

static VALUE
grep_i(VALUE i, VALUE *arg, int argc, VALUE *argv)
{
    ENUM_WANT_SVALUE();

    if (RTEST(rb_funcall(arg[0], id_eqq, 1, i))) {
	rb_ary_push(arg[1], i);
    }
    return Qnil;
}

static VALUE
grep_iter_i(VALUE i, VALUE *arg, int argc, VALUE *argv)
{
    ENUM_WANT_SVALUE();

    if (RTEST(rb_funcall(arg[0], id_eqq, 1, i))) {
	rb_ary_push(arg[1], rb_yield(i));
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.grep(pattern)                   -> array
 *     enum.grep(pattern) {| obj | block }  -> array
 *
 *  Returns an array of every element in <i>enum</i> for which
 *  <code>Pattern === element</code>. If the optional <em>block</em> is
 *  supplied, each matching element is passed to it, and the block's
 *  result is stored in the output array.
 *
 *     (1..100).grep 38..44   #=> [38, 39, 40, 41, 42, 43, 44]
 *     c = IO.constants
 *     c.grep(/SEEK/)         #=> [:SEEK_SET, :SEEK_CUR, :SEEK_END]
 *     res = c.grep(/SEEK/) {|v| IO.const_get(v) }
 *     res                    #=> [0, 1, 2]
 *
 */

static VALUE
enum_grep(VALUE obj, SEL sel, VALUE pat)
{
    VALUE ary = rb_ary_new();
    VALUE arg[2];

    arg[0] = pat;
    arg[1] = ary;

    rb_objc_block_call(obj, selEach, 0, 0, rb_block_given_p() ? grep_iter_i : grep_i, (VALUE)arg);

    return ary;
}

static VALUE
count_i(VALUE i, VALUE memop, int argc, VALUE *argv)
{
    VALUE *memo = (VALUE*)memop;

    ENUM_WANT_SVALUE();

    if (rb_equal(i, memo[1])) {
	memo[0]++;
    }
    return Qnil;
}

static VALUE
count_iter_i(VALUE i, VALUE memop, int argc, VALUE *argv)
{
    VALUE *memo = (VALUE*)memop;

    if (RTEST(enum_yield(argc, argv))) {
	memo[0]++;
    }
    return Qnil;
}

static VALUE
count_all_i(VALUE i, VALUE memop, int argc, VALUE *argv)
{
    VALUE *memo = (VALUE*)memop;

    memo[0]++;
    return Qnil;
}

/*
 *  call-seq:
 *     enum.count                   -> int
 *     enum.count(item)             -> int
 *     enum.count {| obj | block }  -> int
 *
 *  Returns the number of items in <i>enum</i>, where #size is called
 *  if it responds to it, otherwise the items are counted through
 *  enumeration.  If an argument is given, counts the number of items
 *  in <i>enum</i>, for which equals to <i>item</i>.  If a block is
 *  given, counts the number of elements yielding a true value.
 *
 *     ary = [1, 2, 4, 2]
 *     ary.count             #=> 4
 *     ary.count(2)          #=> 2
 *     ary.count{|x|x%2==0}  #=> 3
 *
 */

static VALUE
enum_count(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE memo[2];	/* [count, condition value] */
    rb_block_call_func *func;

    if (argc == 0) {
	if (rb_block_given_p()) {
	    func = count_iter_i;
	}
	else {
	    if (rb_respond_to(obj, id_size)) {
		return rb_funcall(obj, id_size, 0, 0);
	    }
	    func = count_all_i;
	}
    }
    else {
	rb_scan_args(argc, argv, "1", &memo[1]);
	if (rb_block_given_p()) {
	    rb_warn("given block not used");
	}
        func = count_i;
    }

    memo[0] = 0;
    rb_objc_block_call(obj, selEach, 0, 0, func, (VALUE)&memo);
    return INT2NUM(memo[0]);
}

static VALUE
find_i(VALUE i, VALUE *memo, int argc, VALUE *argv)
{
    ENUM_WANT_SVALUE();

    if (RTEST(rb_yield(i))) {
	*memo = i;
	rb_iter_break();
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.detect(ifnone = nil) {| obj | block }  -> obj or nil
 *     enum.find(ifnone = nil)   {| obj | block }  -> obj or nil
 *     enum.detect(ifnone = nil)                   -> an_enumerator
 *     enum.find(ifnone = nil)                     -> an_enumerator
 *
 *  Passes each entry in <i>enum</i> to <em>block</em>. Returns the
 *  first for which <em>block</em> is not false.  If no
 *  object matches, calls <i>ifnone</i> and returns its result when it
 *  is specified, or returns <code>nil</code> otherwise.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     (1..10).detect  {|i| i % 5 == 0 and i % 7 == 0 }   #=> nil
 *     (1..100).detect {|i| i % 5 == 0 and i % 7 == 0 }   #=> 35
 *
 */

static VALUE
enum_find(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE memo = Qundef;
    VALUE if_none;

    rb_scan_args(argc, argv, "01", &if_none);
    RETURN_ENUMERATOR(obj, argc, argv);
    rb_objc_block_call(obj, selEach, 0, 0, find_i, (VALUE)&memo);
    if (memo != Qundef) {
	return memo;
    }
    if (!NIL_P(if_none)) {
	return rb_funcall(if_none, rb_intern("call"), 0, 0);
    }
    return Qnil;
}

static VALUE
find_index_i(VALUE i, VALUE memop, int argc, VALUE *argv)
{
    VALUE *memo = (VALUE*)memop;

    ENUM_WANT_SVALUE();

    if (rb_equal(i, memo[2])) {
	memo[0] = UINT2NUM(memo[1]);
	rb_iter_break();
    }
    memo[1]++;
    return Qnil;
}

static VALUE
find_index_iter_i(VALUE i, VALUE memop, int argc, VALUE *argv)
{
    VALUE *memo = (VALUE*)memop;

    if (RTEST(enum_yield(argc, argv))) {
	memo[0] = UINT2NUM(memo[1]);
	rb_iter_break();
    }
    memo[1]++;
    return Qnil;
}

/*
 *  call-seq:
 *     enum.find_index(value)            -> int or nil
 *     enum.find_index {| obj | block }  -> int or nil
 *     enum.find_index                   -> an_enumerator
 *
 *  Compares each entry in <i>enum</i> with <em>value</em> or passes
 *  to <em>block</em>.  Returns the index for the first for which the
 *  evaluated value is non-false.  If no object matches, returns
 *  <code>nil</code>
 *
 *  If neither block nor argument is given, an enumerator is returned instead.
 *
 *     (1..10).find_index  {|i| i % 5 == 0 and i % 7 == 0 }   #=> nil
 *     (1..100).find_index {|i| i % 5 == 0 and i % 7 == 0 }   #=> 34
 *     (1..100).find_index(50)                                #=> 49
 *
 */

static VALUE
enum_find_index(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE memo[3];	/* [return value, current index, condition value] */
    rb_block_call_func *func;

    if (argc == 0) {
        RETURN_ENUMERATOR(obj, 0, 0);
        func = find_index_iter_i;
    }
    else {
	rb_scan_args(argc, argv, "1", &memo[2]);
	if (rb_block_given_p()) {
	    rb_warn("given block not used");
	}
        func = find_index_i;
    }

    memo[0] = Qnil;
    memo[1] = 0;
    rb_objc_block_call(obj, selEach, 0, 0, func, (VALUE)memo);
    return memo[0];
}

static VALUE
find_all_i(VALUE i, VALUE ary, int argc, VALUE *argv)
{
    ENUM_WANT_SVALUE();

    if (RTEST(rb_yield(i))) {
	rb_ary_push(ary, i);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.find_all {| obj | block }  -> array
 *     enum.select   {| obj | block }  -> array
 *     enum.find_all                   -> an_enumerator
 *     enum.select                     -> an_enumerator
 *
 *  Returns an array containing all elements of <i>enum</i> for which
 *  <em>block</em> is not <code>false</code> (see also
 *  <code>Enumerable#reject</code>).
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *
 *     (1..10).find_all {|i|  i % 3 == 0 }   #=> [3, 6, 9]
 *
 */

static VALUE
enum_find_all(VALUE obj, SEL sel)
{
    VALUE ary;

    RETURN_ENUMERATOR(obj, 0, 0);

    ary = rb_ary_new();
    rb_objc_block_call(obj, selEach, 0, 0, find_all_i, ary);

    return ary;
}

static VALUE
reject_i(VALUE i, VALUE ary, int argc, VALUE *argv)
{
    ENUM_WANT_SVALUE();

    if (!RTEST(rb_yield(i))) {
	rb_ary_push(ary, i);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.reject {| obj | block }  -> array
 *     enum.reject                   -> an_enumerator
 *
 *  Returns an array for all elements of <i>enum</i> for which
 *  <em>block</em> is false (see also <code>Enumerable#find_all</code>).
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     (1..10).reject {|i|  i % 3 == 0 }   #=> [1, 2, 4, 5, 7, 8, 10]
 *
 */

static VALUE
enum_reject(VALUE obj, SEL sel)
{
    VALUE ary;

    RETURN_ENUMERATOR(obj, 0, 0);

    ary = rb_ary_new();
    rb_objc_block_call(obj, selEach, 0, 0, reject_i, ary);

    return ary;
}

static VALUE
collect_i(VALUE i, VALUE ary, int argc, VALUE *argv)
{
    rb_ary_push(ary, enum_yield(argc, argv));

    return Qnil;
}

static VALUE
collect_all(VALUE i, VALUE ary, int argc, VALUE *argv)
{
    rb_ary_push(ary, enum_values_pack(argc, argv));

    return Qnil;
}

/*
 *  call-seq:
 *     enum.collect {| obj | block }  -> array
 *     enum.map     {| obj | block }  -> array
 *     enum.collect                   -> an_enumerator
 *     enum.map                       -> an_enumerator
 *
 *  Returns a new array with the results of running <em>block</em> once
 *  for every element in <i>enum</i>.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     (1..4).collect {|i| i*i }   #=> [1, 4, 9, 16]
 *     (1..4).collect { "cat"  }   #=> ["cat", "cat", "cat", "cat"]
 *
 */

static VALUE
enum_collect(VALUE obj, SEL sel)
{
    VALUE ary;

    RETURN_ENUMERATOR(obj, 0, 0);

    ary = rb_ary_new();
    rb_objc_block_call(obj, selEach, 0, 0, collect_i, ary);

    return ary;
}

static VALUE
flat_map_i(VALUE i, VALUE ary, int argc, VALUE *argv)
{
    VALUE tmp;

    i = enum_yield(argc, argv);
    tmp = rb_check_array_type(i);

    if (NIL_P(tmp)) {
	rb_ary_push(ary, i);
    }
    else {
	rb_ary_concat(ary, tmp);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.flat_map       {| obj | block }  -> array
 *     enum.collect_concat {| obj | block }  -> array
 *     enum.flat_map                         -> an_enumerator
 *     enum.collect_concat                   -> an_enumerator
 *
 *  Returns a new array with the concatenated results of running
 *  <em>block</em> once for every element in <i>enum</i>.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     [[1,2],[3,4]].flat_map {|i| i }   #=> [1, 2, 3, 4]
 *
 */

static VALUE
enum_flat_map(VALUE obj, SEL sel)
{
    VALUE ary;

    RETURN_ENUMERATOR(obj, 0, 0);

    ary = rb_ary_new();
    rb_objc_block_call(obj, selEach, 0, 0, flat_map_i, ary);

    return ary;
}

/*
 *  call-seq:
 *     enum.to_a      ->    array
 *     enum.entries   ->    array
 *
 *  Returns an array containing the items in <i>enum</i>.
 *
 *     (1..7).to_a                       #=> [1, 2, 3, 4, 5, 6, 7]
 *     { 'a'=>1, 'b'=>2, 'c'=>3 }.to_a   #=> [["a", 1], ["b", 2], ["c", 3]]
 */
static VALUE
enum_to_a(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE ary = rb_ary_new();

    rb_objc_block_call(obj, selEach, argc, argv, collect_all, ary);
    OBJ_INFECT(ary, obj);

    return ary;
}

static VALUE
inject_i(VALUE i, VALUE p, int argc, VALUE *argv)
{
    VALUE *memo = (VALUE *)p;

    ENUM_WANT_SVALUE();

    if (memo[0] == Qundef) {
	memo[0] = i;
    }
    else {
	memo[0] = rb_yield_values(2, memo[0], i);
    }
    return Qnil;
}

static VALUE
inject_op_i(VALUE i, VALUE p, int argc, VALUE *argv)
{
    VALUE *memo = (VALUE *)p;

    ENUM_WANT_SVALUE();

    if (memo[0] == Qundef) {
	memo[0] = i;
    }
    else {
	memo[0] = rb_funcall(memo[0], (ID)memo[1], 1, i);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.inject(initial, sym) -> obj
 *     enum.inject(sym)          -> obj
 *     enum.inject(initial) {| memo, obj | block }  -> obj
 *     enum.inject          {| memo, obj | block }  -> obj
 *
 *     enum.reduce(initial, sym) -> obj
 *     enum.reduce(sym)          -> obj
 *     enum.reduce(initial) {| memo, obj | block }  -> obj
 *     enum.reduce          {| memo, obj | block }  -> obj
 *
 *  Combines all elements of <i>enum</i> by applying a binary
 *  operation, specified by a block or a symbol that names a
 *  method or operator.
 *
 *  If you specify a block, then for each element in <i>enum</i>
 *  the block is passed an accumulator value (<i>memo</i>) and the element.
 *  If you specify a symbol instead, then each element in the collection
 *  will be passed to the named method of <i>memo</i>.
 *  In either case, the result becomes the new value for <i>memo</i>.
 *  At the end of the iteration, the final value of <i>memo</i> is the
 *  return value fo the method.
 *
 *  If you do not explicitly specify an <i>initial</i> value for <i>memo</i>,
 *  then uses the first element of collection is used as the initial value
 *  of <i>memo</i>.
 *
 *  Examples:
 *
 *     # Sum some numbers
 *     (5..10).reduce(:+)                            #=> 45
 *     # Same using a block and inject
 *     (5..10).inject {|sum, n| sum + n }            #=> 45
 *     # Multiply some numbers
 *     (5..10).reduce(1, :*)                         #=> 151200
 *     # Same using a block
 *     (5..10).inject(1) {|product, n| product * n } #=> 151200
 *     # find the longest word
 *     longest = %w{ cat sheep bear }.inject do |memo,word|
 *        memo.length > word.length ? memo : word
 *     end
 *     longest                                       #=> "sheep"
 *
 */
static VALUE
enum_inject(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE memo[2];
    VALUE (*iter)(VALUE, VALUE, int, VALUE*) = inject_i;

    switch (rb_scan_args(argc, argv, "02", &memo[0], &memo[1])) {
      case 0:
	memo[0] = Qundef;
	break;
      case 1:
	if (rb_block_given_p()) {
	    break;
	}
	memo[1] = (VALUE)rb_to_id(memo[0]);
	memo[0] = Qundef;
	iter = inject_op_i;
	break;
      case 2:
	if (rb_block_given_p()) {
	    rb_warning("given block not used");
	}
	memo[1] = (VALUE)rb_to_id(memo[1]);
	iter = inject_op_i;
	break;
    }
    rb_objc_block_call(obj, selEach, 0, 0, iter, (VALUE)memo);
    if (memo[0] == Qundef) {
	return Qnil;
    }
    return memo[0];
}

static VALUE
partition_i(VALUE i, VALUE *ary, int argc, VALUE *argv)
{
    ENUM_WANT_SVALUE();

    if (RTEST(rb_yield(i))) {
	rb_ary_push(ary[0], i);
    }
    else {
	rb_ary_push(ary[1], i);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.partition {| obj | block }  -> [ true_array, false_array ]
 *     enum.partition                   -> an_enumerator
 *
 *  Returns two arrays, the first containing the elements of
 *  <i>enum</i> for which the block evaluates to true, the second
 *  containing the rest.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     (1..6).partition {|i| (i&1).zero?}   #=> [[2, 4, 6], [1, 3, 5]]
 *
 */

static VALUE
enum_partition(VALUE obj, SEL sel)
{
    VALUE ary[2];

    RETURN_ENUMERATOR(obj, 0, 0);

    ary[0] = rb_ary_new();
    ary[1] = rb_ary_new();
    rb_objc_block_call(obj, selEach, 0, 0, partition_i, (VALUE)ary);

    return rb_assoc_new(ary[0], ary[1]);
}

static VALUE
group_by_i(VALUE i, VALUE hash, int argc, VALUE *argv)
{
    VALUE group;
    VALUE values;

    ENUM_WANT_SVALUE();

    group = rb_yield(i);
    values = rb_hash_aref(hash, group);
    if (NIL_P(values)) {
	values = rb_ary_new4(1, &i);
	rb_hash_aset(hash, group, values);
    }
    else {
	rb_ary_push(values, i);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.group_by {| obj | block }  -> a_hash
 *     enum.group_by                   -> an_enumerator
 *
 *  Returns a hash, which keys are evaluated result from the
 *  block, and values are arrays of elements in <i>enum</i>
 *  corresponding to the key.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     (1..6).group_by {|i| i%3}   #=> {0=>[3, 6], 1=>[1, 4], 2=>[2, 5]}
 *
 */

static VALUE
enum_group_by(VALUE obj, SEL sel)
{
    VALUE hash;

    RETURN_ENUMERATOR(obj, 0, 0);

    hash = rb_hash_new();
    rb_objc_block_call(obj, selEach, 0, 0, group_by_i, hash);
    OBJ_INFECT(hash, obj);

    return hash;
}

static VALUE
first_i(VALUE i, VALUE *params, int argc, VALUE *argv)
{
    ENUM_WANT_SVALUE();

    if (NIL_P(params[1])) {
	params[1] = i;
	rb_iter_break();
    }
    else {
	long n = params[0];

	rb_ary_push(params[1], i);
	n--;
	if (n <= 0) {
	    rb_iter_break();
	}
	params[0] = n;
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.first       ->  obj or nil
 *     enum.first(n)    ->  an_array
 *
 *  Returns the first element, or the first +n+ elements, of the enumerable.
 *  If the enumerable is empty, the first form returns <code>nil</code>, and the
 *  second form returns an empty array.
 *
 */

static VALUE
enum_first(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE n, params[2];

    if (argc == 0) {
	params[0] = params[1] = Qnil;
    }
    else {
	long len;

	rb_scan_args(argc, argv, "01", &n);
	len = NUM2LONG(n);
	if (len == 0) return rb_ary_new2(0);
	if (len < 0) {
	    rb_raise(rb_eArgError, "negative length");
	}
	params[0] = len;
	params[1] = rb_ary_new2(len);
    }
    rb_objc_block_call(obj, selEach, 0, 0, first_i, (VALUE)params);

    return params[1];
}


/*
 *  call-seq:
 *     enum.sort                     -> array
 *     enum.sort {| a, b | block }   -> array
 *
 *  Returns an array containing the items in <i>enum</i> sorted,
 *  either according to their own <code><=></code> method, or by using
 *  the results of the supplied block. The block should return -1, 0, or
 *  +1 depending on the comparison between <i>a</i> and <i>b</i>. As of
 *  Ruby 1.8, the method <code>Enumerable#sort_by</code> implements a
 *  built-in Schwartzian Transform, useful when key computation or
 *  comparison is expensive.
 *
 *     %w(rhea kea flea).sort         #=> ["flea", "kea", "rhea"]
 *     (1..10).sort {|a,b| b <=> a}   #=> [10, 9, 8, 7, 6, 5, 4, 3, 2, 1]
 */

static VALUE
enum_sort(VALUE obj, SEL sel)
{
    return rb_ary_sort(enum_to_a(obj, 0, 0, 0));
}

static VALUE
sort_by_i(VALUE i, VALUE ary, int argc, VALUE *argv)
{
    NODE *memo;

    ENUM_WANT_SVALUE();
#if !WITH_OBJC
    if (RBASIC(ary)->klass) {
	rb_raise(rb_eRuntimeError, "sort_by reentered");
    }
#endif
    /* use NODE_DOT2 as memo(v, v, -) */
    memo = rb_node_newnode(NODE_DOT2, rb_yield(i), i, 0);
    rb_ary_push(ary, (VALUE)memo);
    return Qnil;
}

static int
sort_by_cmp(void *data, const void *ap, const void *bp)
{
    VALUE a = (*(NODE *const *)ap)->u1.value;
    VALUE b = (*(NODE *const *)bp)->u1.value;
    return rb_cmpint(rb_objs_cmp(a, b), a, b);
}

/*
 *  call-seq:
 *     enum.sort_by {| obj | block }    -> array
 *     enum.sort_by                     -> an_enumerator
 *
 *  Sorts <i>enum</i> using a set of keys generated by mapping the
 *  values in <i>enum</i> through the given block.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     %w{ apple pear fig }.sort_by {|word| word.length}
 *                   #=> ["fig", "pear", "apple"]
 *
 *  The current implementation of <code>sort_by</code> generates an
 *  array of tuples containing the original collection element and the
 *  mapped value. This makes <code>sort_by</code> fairly expensive when
 *  the keysets are simple
 *
 *     require 'benchmark'
 *
 *     a = (1..100000).map {rand(100000)}
 *
 *     Benchmark.bm(10) do |b|
 *       b.report("Sort")    { a.sort }
 *       b.report("Sort by") { a.sort_by {|a| a} }
 *     end
 *
 *  <em>produces:</em>
 *
 *     user     system      total        real
 *     Sort        0.180000   0.000000   0.180000 (  0.175469)
 *     Sort by     1.980000   0.040000   2.020000 (  2.013586)
 *
 *  However, consider the case where comparing the keys is a non-trivial
 *  operation. The following code sorts some files on modification time
 *  using the basic <code>sort</code> method.
 *
 *     files = Dir["*"]
 *     sorted = files.sort {|a,b| File.new(a).mtime <=> File.new(b).mtime}
 *     sorted   #=> ["mon", "tues", "wed", "thurs"]
 *
 *  This sort is inefficient: it generates two new <code>File</code>
 *  objects during every comparison. A slightly better technique is to
 *  use the <code>Kernel#test</code> method to generate the modification
 *  times directly.
 *
 *     files = Dir["*"]
 *     sorted = files.sort { |a,b|
 *       test(?M, a) <=> test(?M, b)
 *     }
 *     sorted   #=> ["mon", "tues", "wed", "thurs"]
 *
 *  This still generates many unnecessary <code>Time</code> objects. A
 *  more efficient technique is to cache the sort keys (modification
 *  times in this case) before the sort. Perl users often call this
 *  approach a Schwartzian Transform, after Randal Schwartz. We
 *  construct a temporary array, where each element is an array
 *  containing our sort key along with the filename. We sort this array,
 *  and then extract the filename from the result.
 *
 *     sorted = Dir["*"].collect { |f|
 *        [test(?M, f), f]
 *     }.sort.collect { |f| f[1] }
 *     sorted   #=> ["mon", "tues", "wed", "thurs"]
 *
 *  This is exactly what <code>sort_by</code> does internally.
 *
 *     sorted = Dir["*"].sort_by {|f| test(?M, f)}
 *     sorted   #=> ["mon", "tues", "wed", "thurs"]
 */

static VALUE
enum_sort_by(VALUE obj, SEL sel)
{
    RETURN_ENUMERATOR(obj, 0, 0);

    VALUE ary;
    if (TYPE(obj) == T_ARRAY) {
	ary  = rb_ary_new2(RARRAY_LEN(obj));
    }
    else {
	ary = rb_ary_new();
    }
    rb_objc_block_call(obj, selEach, 0, 0, sort_by_i, ary);
    if (RARRAY_LEN(ary) > 1) {
	qsort_r((VALUE *)RARRAY_PTR(ary), RARRAY_LEN(ary), sizeof(VALUE), NULL,
		sort_by_cmp);
    }
    for (long i = 0; i < RARRAY_LEN(ary); i++) {
	rb_ary_store(ary, i, RNODE(RARRAY_AT(ary, i))->u2.value);
    }
    OBJ_INFECT(ary, obj);

    return ary;
}

#define ENUMFUNC(name) rb_block_given_p() ? name##_iter_i : name##_i

#define DEFINE_ENUMFUNCS(name) \
static VALUE enum_##name##_func(VALUE result, VALUE *memo); \
\
static VALUE \
name##_i(VALUE i, VALUE *memo, int argc, VALUE *argv) \
{ \
    return enum_##name##_func(enum_values_pack(argc, argv), memo); \
} \
\
static VALUE \
name##_iter_i(VALUE i, VALUE *memo, int argc, VALUE *argv) \
{ \
    return enum_##name##_func(enum_yield(argc, argv), memo); \
} \
\
static VALUE \
enum_##name##_func(VALUE result, VALUE *memo)

DEFINE_ENUMFUNCS(all)
{
    if (!RTEST(result)) {
	*memo = Qfalse;
	rb_iter_break();
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.all? [{|obj| block } ]   -> true or false
 *
 *  Passes each element of the collection to the given block. The method
 *  returns <code>true</code> if the block never returns
 *  <code>false</code> or <code>nil</code>. If the block is not given,
 *  Ruby adds an implicit block of <code>{|obj| obj}</code> (that is
 *  <code>all?</code> will return <code>true</code> only if none of the
 *  collection members are <code>false</code> or <code>nil</code>.)
 *
 *     %w{ant bear cat}.all? {|word| word.length >= 3}   #=> true
 *     %w{ant bear cat}.all? {|word| word.length >= 4}   #=> false
 *     [ nil, true, 99 ].all?                            #=> false
 *
 */

static VALUE
enum_all(VALUE obj, SEL sel)
{
    VALUE result = Qtrue;

    rb_objc_block_call(obj, selEach, 0, 0, ENUMFUNC(all), (VALUE)&result);
    return result;
}

DEFINE_ENUMFUNCS(any)
{
    if (RTEST(result)) {
	*memo = Qtrue;
	rb_iter_break();
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.any? [{|obj| block } ]   -> true or false
 *
 *  Passes each element of the collection to the given block. The method
 *  returns <code>true</code> if the block ever returns a value other
 *  than <code>false</code> or <code>nil</code>. If the block is not
 *  given, Ruby adds an implicit block of <code>{|obj| obj}</code> (that
 *  is <code>any?</code> will return <code>true</code> if at least one
 *  of the collection members is not <code>false</code> or
 *  <code>nil</code>.
 *
 *     %w{ant bear cat}.any? {|word| word.length >= 3}   #=> true
 *     %w{ant bear cat}.any? {|word| word.length >= 4}   #=> true
 *     [ nil, true, 99 ].any?                            #=> true
 *
 */

static VALUE
enum_any(VALUE obj, SEL sel)
{
    VALUE result = Qfalse;

    rb_objc_block_call(obj, selEach, 0, 0, ENUMFUNC(any), (VALUE)&result);
    return result;
}

DEFINE_ENUMFUNCS(one)
{
    if (RTEST(result)) {
	if (*memo == Qundef) {
	    *memo = Qtrue;
	}
	else if (*memo == Qtrue) {
	    *memo = Qfalse;
	    rb_iter_break();
	}
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.one? [{|obj| block }]   -> true or false
 *
 *  Passes each element of the collection to the given block. The method
 *  returns <code>true</code> if the block returns <code>true</code>
 *  exactly once. If the block is not given, <code>one?</code> will return
 *  <code>true</code> only if exactly one of the collection members is
 *  true.
 *
 *     %w{ant bear cat}.one? {|word| word.length == 4}   #=> true
 *     %w{ant bear cat}.one? {|word| word.length > 4}    #=> false
 *     %w{ant bear cat}.one? {|word| word.length < 4}    #=> false
 *     [ nil, true, 99 ].one?                            #=> false
 *     [ nil, true, false ].one?                         #=> true
 *
 */

static VALUE
enum_one(VALUE obj, SEL sel)
{
    VALUE result = Qundef;

    rb_objc_block_call(obj, selEach, 0, 0, ENUMFUNC(one), (VALUE)&result);
    if (result == Qundef) return Qfalse;
    return result;
}

DEFINE_ENUMFUNCS(none)
{
    if (RTEST(result)) {
	*memo = Qfalse;
	rb_iter_break();
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.none? [{|obj| block }]   -> true or false
 *
 *  Passes each element of the collection to the given block. The method
 *  returns <code>true</code> if the block never returns <code>true</code>
 *  for all elements. If the block is not given, <code>none?</code> will return
 *  <code>true</code> only if none of the collection members is true.
 *
 *     %w{ant bear cat}.none? {|word| word.length == 5}  #=> true
 *     %w{ant bear cat}.none? {|word| word.length >= 4}  #=> false
 *     [].none?                                          #=> true
 *     [nil].none?                                       #=> true
 *     [nil,false].none?                                 #=> true
 */
static VALUE
enum_none(VALUE obj, SEL sel)
{
    VALUE result = Qtrue;

    rb_objc_block_call(obj, selEach, 0, 0, ENUMFUNC(none), (VALUE)&result);
    return result;
}

static VALUE
min_i(VALUE i, VALUE *memo, int argc, VALUE *argv)
{
    VALUE cmp;

    ENUM_WANT_SVALUE();

    if (*memo == Qundef) {
	*memo = i;
    }
    else {
	cmp = rb_objs_cmp(i, *memo);
	if (rb_cmpint(cmp, i, *memo) < 0) {
	    *memo = i;
	}
    }
    return Qnil;
}

static VALUE
min_ii(VALUE i, VALUE *memo, int argc, VALUE *argv)
{
    VALUE cmp;

    ENUM_WANT_SVALUE();

    if (*memo == Qundef) {
	*memo = i;
    }
    else {
	cmp = rb_yield_values(2, i, *memo);
	if (rb_cmpint(cmp, i, *memo) < 0) {
	    *memo = i;
	}
    }
    return Qnil;
}


/*
 *  call-seq:
 *     enum.min                    -> obj
 *     enum.min {| a,b | block }   -> obj
 *
 *  Returns the object in <i>enum</i> with the minimum value. The
 *  first form assumes all objects implement <code>Comparable</code>;
 *  the second uses the block to return <em>a <=> b</em>.
 *
 *     a = %w(albatross dog horse)
 *     a.min                                  #=> "albatross"
 *     a.min {|a,b| a.length <=> b.length }   #=> "dog"
 */

static VALUE
enum_min(VALUE obj, SEL sel)
{
    VALUE result = Qundef;

    if (rb_block_given_p()) {
	rb_objc_block_call(obj, selEach, 0, 0, min_ii, (VALUE)&result);
    }
    else {
	rb_objc_block_call(obj, selEach, 0, 0, min_i, (VALUE)&result);
    }
    if (result == Qundef) return Qnil;
    return result;
}

static VALUE
max_i(VALUE i, VALUE *memo, int argc, VALUE *argv)
{
    VALUE cmp;

    ENUM_WANT_SVALUE();

    if (*memo == Qundef) {
	*memo = i;
    }
    else {
	cmp = rb_objs_cmp(i, *memo);
	if (rb_cmpint(cmp, i, *memo) > 0) {
	    *memo = i;
	}
    }
    return Qnil;
}

static VALUE
max_ii(VALUE i, VALUE *memo, int argc, VALUE *argv)
{
    VALUE cmp;

    ENUM_WANT_SVALUE();

    if (*memo == Qundef) {
	*memo = i;
    }
    else {
	cmp = rb_yield_values(2, i, *memo);
	if (rb_cmpint(cmp, i, *memo) > 0) {
	    *memo = i;
	}
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.max                   -> obj
 *     enum.max {|a,b| block }    -> obj
 *
 *  Returns the object in _enum_ with the maximum value. The
 *  first form assumes all objects implement <code>Comparable</code>;
 *  the second uses the block to return <em>a <=> b</em>.
 *
 *     a = %w(albatross dog horse)
 *     a.max                                  #=> "horse"
 *     a.max {|a,b| a.length <=> b.length }   #=> "albatross"
 */

static VALUE
enum_max(VALUE obj, SEL sel)
{
    VALUE result = Qundef;

    if (rb_block_given_p()) {
	rb_objc_block_call(obj, selEach, 0, 0, max_ii, (VALUE)&result);
    }
    else {
	rb_objc_block_call(obj, selEach, 0, 0, max_i, (VALUE)&result);
    }
    if (result == Qundef) return Qnil;
    return result;
}

struct minmax_t {
    VALUE min;
    VALUE max;
    VALUE last;
};

static void
minmax_i_update(VALUE i, VALUE j, struct minmax_t *memo)
{
    int n;

    if (memo->min == Qundef) {
	memo->min = i;
	memo->max = j;
    }
    else {
	n = rb_cmpint(rb_objs_cmp(i, memo->min), i, memo->min);
	if (n < 0) {
	    memo->min = i;
	}
	n = rb_cmpint(rb_objs_cmp(j, memo->max), j, memo->max);
	if (n > 0) {
	    memo->max = j;
	}
    }
}

static VALUE
minmax_i(VALUE i, VALUE _memo, int argc, VALUE *argv)
{
    struct minmax_t *memo = (struct minmax_t *)_memo;
    int n;
    VALUE j;

    ENUM_WANT_SVALUE();

    if (memo->last == Qundef) {
        memo->last = i;
        return Qnil;
    }
    j = memo->last;
    memo->last = Qundef;

    n = rb_cmpint(rb_objs_cmp(j, i), j, i);
    if (n == 0)
        i = j;
    else if (n < 0) {
        VALUE tmp;
        tmp = i;
        i = j;
        j = tmp;
    }

    minmax_i_update(i, j, memo);

    return Qnil;
}

static void
minmax_ii_update(VALUE i, VALUE j, struct minmax_t *memo)
{
    int n;

    if (memo->min == Qundef) {
	memo->min = i;
	memo->max = j;
    }
    else {
	n = rb_cmpint(rb_yield_values(2, i, memo->min), i, memo->min);
	if (n < 0) {
	    memo->min = i;
	}
	n = rb_cmpint(rb_yield_values(2, j, memo->max), j, memo->max);
	if (n > 0) {
	    memo->max = j;
	}
    }
}

static VALUE
minmax_ii(VALUE i, VALUE _memo, int argc, VALUE *argv)
{
    struct minmax_t *memo = (struct minmax_t *)_memo;
    int n;
    VALUE j;

    ENUM_WANT_SVALUE();

    if (memo->last == Qundef) {
        memo->last = i;
        return Qnil;
    }
    j = memo->last;
    memo->last = Qundef;

    n = rb_cmpint(rb_yield_values(2, j, i), j, i);
    if (n == 0)
        i = j;
    else if (n < 0) {
        VALUE tmp;
        tmp = i;
        i = j;
        j = tmp;
    }

    minmax_ii_update(i, j, memo);

    return Qnil;
}

/*
 *  call-seq:
 *     enum.minmax                   -> [min,max]
 *     enum.minmax {|a,b| block }    -> [min,max]
 *
 *  Returns two elements array which contains the minimum and the
 *  maximum value in the enumerable.  The first form assumes all
 *  objects implement <code>Comparable</code>; the second uses the
 *  block to return <em>a <=> b</em>.
 *
 *     a = %w(albatross dog horse)
 *     a.minmax                                  #=> ["albatross", "horse"]
 *     a.minmax {|a,b| a.length <=> b.length }   #=> ["dog", "albatross"]
 */

static VALUE
enum_minmax(VALUE obj, SEL sel)
{
    struct minmax_t memo;
    VALUE ary = rb_ary_new3(2, Qnil, Qnil);

    memo.min = Qundef;
    memo.last = Qundef;
    if (rb_block_given_p()) {
	rb_objc_block_call(obj, selEach, 0, 0, minmax_ii, (VALUE)&memo);
        if (memo.last != Qundef)
            minmax_ii_update(memo.last, memo.last, &memo);
    }
    else {
	rb_objc_block_call(obj, selEach, 0, 0, minmax_i, (VALUE)&memo);
        if (memo.last != Qundef)
            minmax_i_update(memo.last, memo.last, &memo);
    }
    if (memo.min != Qundef) {
        rb_ary_store(ary, 0, memo.min);
        rb_ary_store(ary, 1, memo.max);
    }
    return ary;
}

static VALUE
min_by_i(VALUE i, VALUE *memo, int argc, VALUE *argv)
{
    VALUE v;

    ENUM_WANT_SVALUE();

    v = rb_yield(i);
    if (memo[0] == Qundef) {
	memo[0] = v;
	memo[1] = i;
    }
    else if (rb_cmpint(rb_objs_cmp(v, memo[0]), v, memo[0]) < 0) {
	memo[0] = v;
	memo[1] = i;
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.min_by {|obj| block }   -> obj
 *     enum.min_by                  -> an_enumerator
 *
 *  Returns the object in <i>enum</i> that gives the minimum
 *  value from the given block.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     a = %w(albatross dog horse)
 *     a.min_by {|x| x.length }   #=> "dog"
 */

static VALUE
enum_min_by(VALUE obj, SEL sel)
{
    VALUE memo[2];

    RETURN_ENUMERATOR(obj, 0, 0);

    memo[0] = Qundef;
    memo[1] = Qnil;
    rb_objc_block_call(obj, selEach, 0, 0, min_by_i, (VALUE)memo);
    return memo[1];
}

static VALUE
max_by_i(VALUE i, VALUE *memo, int argc, VALUE *argv)
{
    VALUE v;

    ENUM_WANT_SVALUE();

    v = rb_yield(i);
    if (memo[0] == Qundef) {
	memo[0] = v;
	memo[1] = i;
    }
    else if (rb_cmpint(rb_objs_cmp(v, memo[0]), v, memo[0]) > 0) {
	memo[0] = v;
	memo[1] = i;
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.max_by {|obj| block }   -> obj
 *     enum.max_by                  -> an_enumerator
 *
 *  Returns the object in <i>enum</i> that gives the maximum
 *  value from the given block.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     a = %w(albatross dog horse)
 *     a.max_by {|x| x.length }   #=> "albatross"
 */

static VALUE
enum_max_by(VALUE obj, SEL sel)
{
    VALUE memo[2];

    RETURN_ENUMERATOR(obj, 0, 0);

    memo[0] = Qundef;
    memo[1] = Qnil;
    rb_objc_block_call(obj, selEach, 0, 0, max_by_i, (VALUE)memo);
    return memo[1];
}

struct minmax_by_t {
    VALUE min_bv;
    VALUE max_bv;
    VALUE min;
    VALUE max;
    VALUE last_bv;
    VALUE last;
};

static void
minmax_by_i_update(VALUE v1, VALUE v2, VALUE i1, VALUE i2, struct minmax_by_t *memo)
{
    if (memo->min_bv == Qundef) {
	memo->min_bv = v1;
	memo->max_bv = v2;
	memo->min = i1;
	memo->max = i2;
    }
    else {
	if (rb_cmpint(rb_objs_cmp(v1, memo->min_bv), v1, memo->min_bv) < 0) {
	    memo->min_bv = v1;
	    memo->min = i1;
	}
	if (rb_cmpint(rb_objs_cmp(v2, memo->max_bv), v2, memo->max_bv) > 0) {
	    memo->max_bv = v2;
	    memo->max = i2;
	}
    }
}

static VALUE
minmax_by_i(VALUE i, VALUE _memo, int argc, VALUE *argv)
{
    struct minmax_by_t *memo = (struct minmax_by_t *)_memo;
    VALUE vi, vj, j;
    int n;

    ENUM_WANT_SVALUE();

    vi = rb_yield(i);

    if (memo->last_bv == Qundef) {
        memo->last_bv = vi;
        memo->last = i;
        return Qnil;
    }
    vj = memo->last_bv;
    j = memo->last;
    memo->last_bv = Qundef;

    n = rb_cmpint(rb_objs_cmp(vj, vi), vj, vi);
    if (n == 0) {
        i = j;
        vi = vj;
    }
    else if (n < 0) {
        VALUE tmp;
        tmp = i;
        i = j;
        j = tmp;
        tmp = vi;
        vi = vj;
        vj = tmp;
    }

    minmax_by_i_update(vi, vj, i, j, memo);

    return Qnil;
}

/*
 *  call-seq:
 *     enum.minmax_by {|obj| block }   -> [min, max]
 *     enum.minmax_by                  -> an_enumerator
 *
 *  Returns two elements array array containing the objects in
 *  <i>enum</i> that gives the minimum and maximum values respectively
 *  from the given block.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     a = %w(albatross dog horse)
 *     a.minmax_by {|x| x.length }   #=> ["dog", "albatross"]
 */

static VALUE
enum_minmax_by(VALUE obj, SEL sel)
{
    struct minmax_by_t memo;

    RETURN_ENUMERATOR(obj, 0, 0);

    memo.min_bv = Qundef;
    memo.max_bv = Qundef;
    memo.min = Qnil;
    memo.max = Qnil;
    memo.last_bv = Qundef;
    memo.last = Qundef;
    rb_block_call(obj, id_each, 0, 0, minmax_by_i, (VALUE)&memo);
    if (memo.last_bv != Qundef)
        minmax_by_i_update(memo.last_bv, memo.last_bv, memo.last, memo.last, &memo);
    return rb_assoc_new(memo.min, memo.max);
}

static VALUE
member_i(VALUE iter, VALUE *memo, int argc, VALUE *argv)
{
    if (rb_equal(enum_values_pack(argc, argv), memo[0])) {
	memo[1] = Qtrue;
	rb_iter_break();
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.include?(obj)     -> true or false
 *     enum.member?(obj)      -> true or false
 *
 *  Returns <code>true</code> if any member of <i>enum</i> equals
 *  <i>obj</i>. Equality is tested using <code>==</code>.
 *
 *     IO.constants.include? :SEEK_SET          #=> true
 *     IO.constants.include? :SEEK_NO_FURTHER   #=> false
 *
 */

static VALUE
enum_member(VALUE obj, SEL sel, VALUE val)
{
    VALUE memo[2];

    memo[0] = val;
    memo[1] = Qfalse;
    rb_objc_block_call(obj, selEach, 0, 0, member_i, (VALUE)memo);
    return memo[1];
}

static VALUE
each_with_index_i(VALUE i, VALUE memo, int argc, VALUE *argv)
{
    long n = (*(VALUE *)memo)++;

    return rb_yield_values(2, enum_values_pack(argc, argv), INT2NUM(n));
}

/*
 *  call-seq:
 *     enum.each_with_index(*args) {|obj, i| block }   ->  enum
 *     enum.each_with_index(*args)                     ->  an_enumerator
 *
 *  Calls <em>block</em> with two arguments, the item and its index,
 *  for each item in <i>enum</i>.  Given arguments are passed through
 *  to #each().
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     hash = Hash.new
 *     %w(cat dog wombat).each_with_index {|item, index|
 *       hash[item] = index
 *     }
 *     hash   #=> {"cat"=>0, "dog"=>1, "wombat"=>2}
 *
 */

static VALUE
enum_each_with_index(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    long memo;

    RETURN_ENUMERATOR(obj, argc, argv);

    memo = 0;
    rb_objc_block_call(obj, selEach, argc, argv, each_with_index_i, (VALUE)&memo);
    return obj;
}


/*
 *  call-seq:
 *     enum.reverse_each(*args) {|item| block }   ->  enum
 *     enum.reverse_each(*args)                   ->  an_enumerator
 *
 *  Builds a temporary array and traverses that array in reverse order.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 */

static VALUE
enum_reverse_each(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE ary;
    long i;

    RETURN_ENUMERATOR(obj, argc, argv);

    ary = enum_to_a(obj, 0, argc, argv);

    for (i = RARRAY_LEN(ary); --i >= 0; ) {
	rb_yield(RARRAY_AT(ary, i));
	RETURN_IF_BROKEN();
    }

    return obj;
}

static VALUE
each_val_i(VALUE i, VALUE p, int argc, VALUE *argv)
{
    ENUM_WANT_SVALUE();
    rb_yield(i);
    return Qnil;
}

/*
 *  call-seq:
 *     enum.each_entry {|obj| block}  -> enum
 *     enum.each_entry                -> an_enumerator
 *
 *  Calls <i>block</i> once for each element in +self+, passing that
 *  element as a parameter, converting multiple values from yield to an
 *  array.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     class Foo
 *       include Enumerable
 *       def each
 *         yield 1
 *         yield 1,2
 *       end
 *     end
 *     Foo.new.each_entry{|o| print o, " -- "}
 *
 *  produces:
 *
 *     1 -- [1, 2] --
 */

static VALUE
enum_each_entry(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    RETURN_ENUMERATOR(obj, argc, argv);
    rb_objc_block_call(obj, selEach, argc, argv, each_val_i, 0);
    return obj;
}

static VALUE
each_slice_i(VALUE i, VALUE *memo, int argc, VALUE *argv)
{
    VALUE ary = memo[0];
    VALUE v = Qnil;
    long size = (long)memo[1];
    ENUM_WANT_SVALUE();

    rb_ary_push(ary, i);

    if (RARRAY_LEN(ary) == size) {
	v = rb_yield(ary);
	memo[0] = rb_ary_new2(size);
    }

    return v;
}

/*
 *  call-seq:
 *    enum.each_slice(n) {...}  ->  nil
 *    enum.each_slice(n)        ->  an_enumerator
 *
 *  Iterates the given block for each slice of <n> elements.  If no
 *  block is given, returns an enumerator.
 *
 *  e.g.:
 *      (1..10).each_slice(3) {|a| p a}
 *      # outputs below
 *      [1, 2, 3]
 *      [4, 5, 6]
 *      [7, 8, 9]
 *      [10]
 *
 */
static VALUE
enum_each_slice(VALUE obj, SEL sel, VALUE n)
{
    long size = NUM2LONG(n);
    VALUE args[2], ary;

    if (size <= 0) {
	rb_raise(rb_eArgError, "invalid slice size");
    }
    RETURN_ENUMERATOR(obj, 1, &n);
    args[0] = rb_ary_new2(size);
    args[1] = (VALUE)size;

    rb_objc_block_call(obj, selEach, 0, 0, each_slice_i, (VALUE)args);

    ary = args[0];
    if (RARRAY_LEN(ary) > 0) {
	rb_yield(ary);
    }

    return Qnil;
}

static VALUE
each_cons_i(VALUE i, VALUE *memo, int argc, VALUE *argv)
{
    VALUE ary = memo[0];
    VALUE v = Qnil;
    long size = (long)memo[1];
    ENUM_WANT_SVALUE();

    if (RARRAY_LEN(ary) == size) {
	rb_ary_shift(ary);
    }
    rb_ary_push(ary, i);
    if (RARRAY_LEN(ary) == size) {
	v = rb_yield(rb_ary_dup(ary));
    }
    return v;
}

/*
 *  call-seq:
 *    enum.each_cons(n) {...}   ->  nil
 *    enum.each_cons(n)         ->  an_enumerator
 *
 *  Iterates the given block for each array of consecutive <n>
 *  elements.  If no block is given, returns an enumerator.
 *
 *  e.g.:
 *      (1..10).each_cons(3) {|a| p a}
 *      # outputs below
 *      [1, 2, 3]
 *      [2, 3, 4]
 *      [3, 4, 5]
 *      [4, 5, 6]
 *      [5, 6, 7]
 *      [6, 7, 8]
 *      [7, 8, 9]
 *      [8, 9, 10]
 *
 */
static VALUE
enum_each_cons(VALUE obj, SEL sel, VALUE n)
{
    long size = NUM2LONG(n);
    VALUE args[2];

    if (size <= 0) {
	rb_raise(rb_eArgError, "invalid size");
    }
    RETURN_ENUMERATOR(obj, 1, &n);
    args[0] = rb_ary_new2(size);
    args[1] = (VALUE)size;

    rb_objc_block_call(obj, selEach, 0, 0, each_cons_i, (VALUE)args);

    return Qnil;
}

static VALUE
each_with_object_i(VALUE i, VALUE memo, int argc, VALUE *argv)
{
    ENUM_WANT_SVALUE();
    return rb_yield_values(2, i, memo);
}

/*
 *  call-seq:
 *    enum.each_with_object(obj) {|(*args), memo_obj| ... }  ->  obj
 *    enum.each_with_object(obj)                             ->  an_enumerator
 *
 *  Iterates the given block for each element with an arbitrary
 *  object given, and returns the initially given object.
 *
 *  If no block is given, returns an enumerator.
 *
 *  e.g.:
 *      evens = (1..10).each_with_object([]) {|i, a| a << i*2 }
 *      #=> [2, 4, 6, 8, 10, 12, 14, 16, 18, 20]
 *
 */
static VALUE
enum_each_with_object(VALUE obj, SEL sel, VALUE memo)
{
    RETURN_ENUMERATOR(obj, 1, &memo);

    rb_objc_block_call(obj, selEach, 0, 0, each_with_object_i, (VALUE)memo);

    return memo;
}

static VALUE
zip_ary(VALUE val, NODE *memo, int argc, VALUE *argv)
{
    volatile VALUE result = memo->u1.value;
    volatile VALUE args = memo->u2.value;
    long n = memo->u3.cnt++;
    volatile VALUE tmp;
    int i;

    tmp = rb_ary_new2(RARRAY_LEN(args) + 1);
    rb_ary_store(tmp, 0, enum_values_pack(argc, argv));
    for (i=0; i<RARRAY_LEN(args); i++) {
	VALUE e = RARRAY_AT(args, i);

	if (RARRAY_LEN(e) <= n) {
	    rb_ary_push(tmp, Qnil);
	}
	else {
	    rb_ary_push(tmp, RARRAY_AT(e, n));
	}
    }
    if (NIL_P(result)) {
	rb_yield(tmp);
    }
    else {
	rb_ary_push(result, tmp);
    }
    return Qnil;
}

static VALUE
call_next(VALUE *v)
{
    return v[0] = rb_funcall(v[1], id_next, 0, 0);
}

static VALUE
call_stop(VALUE *v)
{
    return v[0] = Qundef;
}

static VALUE
zip_i(VALUE val, NODE *memo, int argc, VALUE *argv)
{
    volatile VALUE result = memo->u1.value;
    volatile VALUE args = memo->u2.value;
    volatile VALUE tmp;
    int i;

    tmp = rb_ary_new2(RARRAY_LEN(args) + 1);
    rb_ary_store(tmp, 0, enum_values_pack(argc, argv));
    for (i=0; i<RARRAY_LEN(args); i++) {
	if (NIL_P(RARRAY_AT(args, i))) {
	    rb_ary_push(tmp, Qnil);
	}
	else {
	    VALUE v[2];

	    v[1] = RARRAY_AT(args, i);
	    rb_rescue2(call_next, (VALUE)v, call_stop, (VALUE)v, rb_eStopIteration, 0);
	    if (v[0] == Qundef) {
		rb_ary_store(args, i, Qnil);
		v[0] = Qnil;
	    }
	    rb_ary_push(tmp, v[0]);
	}
    }
    if (NIL_P(result)) {
	rb_yield(tmp);
    }
    else {
	rb_ary_push(result, tmp);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.zip(arg, ...)                   -> an_array_of_array
 *     enum.zip(arg, ...) {|arr| block }    -> nil
 *
 *  Takes one element from <i>enum</i> and merges corresponding
 *  elements from each <i>args</i>.  This generates a sequence of
 *  <em>n</em>-element arrays, where <em>n</em> is one more than the
 *  count of arguments.  The length of the resulting sequence will be
 *  <code>enum#size</code>.  If the size of any argument is less than
 *  <code>enum#size</code>, <code>nil</code> values are supplied. If
 *  a block is given, it is invoked for each output array, otherwise
 *  an array of arrays is returned.
 *
 *     a = [ 4, 5, 6 ]
 *     b = [ 7, 8, 9 ]
 *
 *     [1,2,3].zip(a, b)      #=> [[1, 4, 7], [2, 5, 8], [3, 6, 9]]
 *     [1,2].zip(a,b)         #=> [[1, 4, 7], [2, 5, 8]]
 *     a.zip([1,2],[8])       #=> [[4, 1, 8], [5, 2, nil], [6, nil, nil]]
 *
 */

static VALUE
enum_zip(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    int i;
    ID conv;
    NODE *memo;
    VALUE result = Qnil;
    VALUE args = rb_ary_new4(argc, argv);
    int allary = TRUE;

    argv = (VALUE*)RARRAY_PTR(args);
    for (i = 0; i < argc; i++) {
	VALUE ary = rb_check_array_type(argv[i]);
	if (NIL_P(ary)) {
	    allary = FALSE;
	    break;
	}
	rary_store(args, i, ary);
    }
    if (!allary) {
	conv = rb_intern("to_enum");
	for (i = 0; i < argc; i++) {
	    VALUE res = rb_funcall(argv[i], conv, 1, ID2SYM(id_each));
	    rary_store(args, i, res);
	}
    }
    if (!rb_block_given_p()) {
	result = rb_ary_new();
    }
    /* use NODE_DOT2 as memo(v, v, -) */
    memo = rb_node_newnode(NODE_DOT2, result, args, 0);
    rb_objc_block_call(obj, selEach, 0, 0, allary ? zip_ary : zip_i, (VALUE)memo);

    return result;
}

static VALUE
take_i(VALUE i, VALUE *arg, int argc, VALUE *argv)
{
    rb_ary_push(arg[0], enum_values_pack(argc, argv));
    if (--arg[1] == 0) {
	rb_iter_break();
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.take(n)               -> array
 *
 *  Returns first n elements from <i>enum</i>.
 *
 *     a = [1, 2, 3, 4, 5, 0]
 *     a.take(3)             #=> [1, 2, 3]
 *
 */

static VALUE
enum_take(VALUE obj, SEL sel, VALUE n)
{
    VALUE args[2];
    long len = NUM2LONG(n);

    if (len < 0) {
	rb_raise(rb_eArgError, "attempt to take negative size");
    }

    if (len == 0) {
	return rb_ary_new2(0);
    }
    args[0] = rb_ary_new();
    args[1] = len;
    rb_objc_block_call(obj, selEach, 0, 0, take_i, (VALUE)args);
    return args[0];
}


static VALUE
take_while_i(VALUE i, VALUE *ary, int argc, VALUE *argv)
{
    if (!RTEST(enum_yield(argc, argv))) {
	rb_iter_break();
    }
    rb_ary_push(*ary, enum_values_pack(argc, argv));
    return Qnil;
}

/*
 *  call-seq:
 *     enum.take_while {|arr| block }   -> array
 *     enum.take_while                  -> an_enumerator
 *
 *  Passes elements to the block until the block returns +nil+ or +false+,
 *  then stops iterating and returns an array of all prior elements.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     a = [1, 2, 3, 4, 5, 0]
 *     a.take_while {|i| i < 3 }   #=> [1, 2]
 *
 */

static VALUE
enum_take_while(VALUE obj, SEL sel)
{
    VALUE ary;

    RETURN_ENUMERATOR(obj, 0, 0);
    ary = rb_ary_new();
    rb_objc_block_call(obj, selEach, 0, 0, take_while_i, (VALUE)&ary);
    return ary;
}

static VALUE
drop_i(VALUE i, VALUE *arg, int argc, VALUE *argv)
{
    if (arg[1] == 0) {
	rb_ary_push(arg[0], enum_values_pack(argc, argv));
    }
    else {
	arg[1]--;
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.drop(n)               -> array
 *
 *  Drops first n elements from <i>enum</i>, and returns rest elements
 *  in an array.
 *
 *     a = [1, 2, 3, 4, 5, 0]
 *     a.drop(3)             #=> [4, 5, 0]
 *
 */

static VALUE
enum_drop(VALUE obj, SEL sel, VALUE n)
{
    VALUE args[2];
    long len = NUM2LONG(n);

    if (len < 0) {
	rb_raise(rb_eArgError, "attempt to drop negative size");
    }

    args[1] = len;
    args[0] = rb_ary_new();
    rb_objc_block_call(obj, selEach, 0, 0, drop_i, (VALUE)args);
    return args[0];
}


static VALUE
drop_while_i(VALUE i, VALUE *args, int argc, VALUE *argv)
{
    ENUM_WANT_SVALUE();

    if (!args[1] && !RTEST(rb_yield(i))) {
	args[1] = Qtrue;
    }
    if (args[1]) {
	rb_ary_push(args[0], i);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     enum.drop_while {|arr| block }   -> array
 *     enum.drop_while                  -> an_enumerator
 *
 *  Drops elements up to, but not including, the first element for
 *  which the block returns +nil+ or +false+ and returns an array
 *  containing the remaining elements.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     a = [1, 2, 3, 4, 5, 0]
 *     a.drop_while {|i| i < 3 }   #=> [3, 4, 5, 0]
 *
 */

static VALUE
enum_drop_while(VALUE obj, SEL sel)
{
    VALUE args[2];

    RETURN_ENUMERATOR(obj, 0, 0);
    args[0] = rb_ary_new();
    args[1] = Qfalse;
    rb_objc_block_call(obj, selEach, 0, 0, drop_while_i, (VALUE)args);
    return args[0];
}

static VALUE
cycle_i(VALUE i, VALUE ary, int argc, VALUE *argv)
{
    ENUM_WANT_SVALUE();

    rb_ary_push(ary, i);
    rb_yield(i);
    return Qnil;
}

/*
 *  call-seq:
 *     enum.cycle(n=nil) {|obj| block }   ->  nil
 *     enum.cycle(n=nil)                  ->  an_enumerator
 *
 *  Calls <i>block</i> for each element of <i>enum</i> repeatedly _n_
 *  times or forever if none or +nil+ is given.  If a non-positive
 *  number is given or the collection is empty, does nothing.  Returns
 *  +nil+ if the loop has finished without getting interrupted.
 *
 *  Enumerable#cycle saves elements in an internal array so changes
 *  to <i>enum</i> after the first pass have no effect.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     a = ["a", "b", "c"]
 *     a.cycle {|x| puts x }  # print, a, b, c, a, b, c,.. forever.
 *     a.cycle(2) {|x| puts x }  # print, a, b, c, a, b, c.
 *
 */

static VALUE
enum_cycle(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE ary;
    VALUE nv = Qnil;
    long n, i, len;

    rb_scan_args(argc, argv, "01", &nv);

    RETURN_ENUMERATOR(obj, argc, argv);
    if (NIL_P(nv)) {
        n = -1;
    }
    else {
        n = NUM2LONG(nv);
        if (n <= 0) return Qnil;
    }
    ary = rb_ary_new();
#if !WITH_OBJC
    RBASIC(ary)->klass = 0;
#endif
    rb_objc_block_call(obj, selEach, 0, 0, cycle_i, ary);
    len = RARRAY_LEN(ary);
    if (len == 0) return Qnil;
    while (n < 0 || 0 < --n) {
        for (i=0; i<len; i++) {
            rb_yield(RARRAY_AT(ary, i));
	    RETURN_IF_BROKEN();
        }
    }
    return Qnil;		/* not reached */
}

/*
 *  The <code>Enumerable</code> mixin provides collection classes with
 *  several traversal and searching methods, and with the ability to
 *  sort. The class must provide a method <code>each</code>, which
 *  yields successive members of the collection. If
 *  <code>Enumerable#max</code>, <code>#min</code>, or
 *  <code>#sort</code> is used, the objects in the collection must also
 *  implement a meaningful <code><=></code> operator, as these methods
 *  rely on an ordering between members of the collection.
 */

void
Init_Enumerable(void)
{
    rb_mEnumerable = rb_define_module("Enumerable");

    rb_objc_define_method(rb_mEnumerable, "to_a", enum_to_a, -1);
    rb_objc_define_method(rb_mEnumerable, "entries", enum_to_a, -1);

    rb_objc_define_method(rb_mEnumerable,"sort", enum_sort, 0);
    rb_objc_define_method(rb_mEnumerable,"sort_by", enum_sort_by, 0);
    rb_objc_define_method(rb_mEnumerable,"grep", enum_grep, 1);
#if WITH_OBJC
    /* FIXME we cannot define count because it overlaps with
     * NSArray#count, NSDictionary#count, etc... 
     */
    rb_objc_define_method(rb_mEnumerable,"_count", enum_count, -1);
#else
    rb_objc_define_method(rb_mEnumerable,"count", enum_count, -1);
#endif
    rb_objc_define_method(rb_mEnumerable, "find", enum_find, -1);
    rb_objc_define_method(rb_mEnumerable, "detect", enum_find, -1);
    rb_objc_define_method(rb_mEnumerable, "find_index", enum_find_index, -1);
    rb_objc_define_method(rb_mEnumerable, "find_all", enum_find_all, 0);
    rb_objc_define_method(rb_mEnumerable, "select", enum_find_all, 0);
    rb_objc_define_method(rb_mEnumerable, "reject", enum_reject, 0);
    rb_objc_define_method(rb_mEnumerable, "collect", enum_collect, 0);
    rb_objc_define_method(rb_mEnumerable, "map", enum_collect, 0);
    rb_objc_define_method(rb_mEnumerable, "flat_map", enum_flat_map, 0);
    rb_objc_define_method(rb_mEnumerable, "collect_concat", enum_flat_map, 0);
    rb_objc_define_method(rb_mEnumerable, "inject", enum_inject, -1);
    rb_objc_define_method(rb_mEnumerable, "reduce", enum_inject, -1);
    rb_objc_define_method(rb_mEnumerable, "partition", enum_partition, 0);
    rb_objc_define_method(rb_mEnumerable, "group_by", enum_group_by, 0);
    rb_objc_define_method(rb_mEnumerable, "first", enum_first, -1);
    rb_objc_define_method(rb_mEnumerable, "all?", enum_all, 0);
    rb_objc_define_method(rb_mEnumerable, "any?", enum_any, 0);
    rb_objc_define_method(rb_mEnumerable, "one?", enum_one, 0);
    rb_objc_define_method(rb_mEnumerable, "none?", enum_none, 0);
    rb_objc_define_method(rb_mEnumerable, "min", enum_min, 0);
    rb_objc_define_method(rb_mEnumerable, "max", enum_max, 0);
    rb_objc_define_method(rb_mEnumerable, "minmax", enum_minmax, 0);
    rb_objc_define_method(rb_mEnumerable, "min_by", enum_min_by, 0);
    rb_objc_define_method(rb_mEnumerable, "max_by", enum_max_by, 0);
    rb_objc_define_method(rb_mEnumerable, "minmax_by", enum_minmax_by, 0);
    rb_objc_define_method(rb_mEnumerable, "member?", enum_member, 1);
    rb_objc_define_method(rb_mEnumerable, "include?", enum_member, 1);
    rb_objc_define_method(rb_mEnumerable, "each_with_index", enum_each_with_index, -1);
    rb_objc_define_method(rb_mEnumerable, "reverse_each", enum_reverse_each, -1);
    rb_objc_define_method(rb_mEnumerable, "each_entry", enum_each_entry, -1);
    rb_objc_define_method(rb_mEnumerable, "each_slice", enum_each_slice, 1);
    rb_objc_define_method(rb_mEnumerable, "each_cons", enum_each_cons, 1);
    rb_objc_define_method(rb_mEnumerable, "each_with_object", enum_each_with_object, 1);
    rb_objc_define_method(rb_mEnumerable, "zip", enum_zip, -1);
    rb_objc_define_method(rb_mEnumerable, "take", enum_take, 1);
    rb_objc_define_method(rb_mEnumerable, "take_while", enum_take_while, 0);
    rb_objc_define_method(rb_mEnumerable, "drop", enum_drop, 1);
    rb_objc_define_method(rb_mEnumerable, "drop_while", enum_drop_while, 0);
    rb_objc_define_method(rb_mEnumerable, "cycle", enum_cycle, -1);

    id_eqq  = rb_intern("===");
    id_each = rb_intern("each");
    id_next = rb_intern("next");
    id_size = rb_intern("size");
}

