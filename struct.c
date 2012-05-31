/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#include "macruby_internal.h"
#include "ruby/node.h"
#include "vm.h"
#include "class.h"

VALUE rb_cStruct;
static ID id_members;

static VALUE struct_alloc(VALUE);

static inline VALUE
struct_ivar_get(VALUE c, ID id)
{
    for (;;) {
	if (rb_ivar_defined(c, id))
	    return rb_ivar_get(c, id);
	c = RCLASS_SUPER(c);
	if (c == 0 || c == rb_cStruct)
	    return Qnil;
    }
}

VALUE
rb_struct_iv_get(VALUE c, const char *name)
{
    return struct_ivar_get(c, rb_intern(name));
}

VALUE
rb_struct_s_members(VALUE klass)
{
    VALUE members = struct_ivar_get(klass, id_members);

    if (NIL_P(members)) {
	rb_raise(rb_eTypeError, "uninitialized struct");
    }
    if (TYPE(members) != T_ARRAY) {
	rb_raise(rb_eTypeError, "corrupted struct");
    }
    return members;
}

VALUE
rb_struct_members(VALUE s)
{
    VALUE members = rb_struct_s_members(rb_obj_class(s));

    if (RSTRUCT_LEN(s) != RARRAY_LEN(members)) {
	rb_raise(rb_eTypeError, "struct size differs (%ld required %ld given)",
		 RARRAY_LEN(members), RSTRUCT_LEN(s));
    }
    return members;
}

static VALUE
rb_struct_s_members_m(VALUE klass)
{
#if WITH_OBJC
    return rb_ary_dup(rb_struct_s_members(klass));
#else
    VALUE members, ary;
    VALUE *p, *pend;

    members = rb_struct_s_members(klass);
    ary = rb_ary_new2(RARRAY_LEN(members));
    p = RARRAY_PTR(members); pend = p + RARRAY_LEN(members);
    while (p < pend) {
	rb_ary_push(ary, *p);
	p++;
    }

    return ary;
#endif
}

/*
 *  call-seq:
 *     struct.members    -> array
 *
 *  Returns an array of strings representing the names of the instance
 *  variables.
 *
 *     Customer = Struct.new(:name, :address, :zip)
 *     joe = Customer.new("Joe Smith", "123 Maple, Anytown NC", 12345)
 *     joe.members   #=> [:name, :address, :zip]
 */

static VALUE
rb_struct_members_m(VALUE obj, SEL sel)
{
    return rb_struct_s_members_m(rb_obj_class(obj));
}

VALUE
rb_struct_getmember(VALUE obj, ID id)
{
    VALUE members, slot, *ptr;
    long i, len;

    ptr = RSTRUCT_PTR(obj);
    members = rb_struct_members(obj);
    slot = ID2SYM(id);
    len = RARRAY_LEN(members);
    for (i=0; i<len; i++) {
	if (RARRAY_AT(members, i) == slot) {
	    return ptr[i];
	}
    }
    rb_name_error(id, "%s is not struct member", rb_id2name(id));
    return Qnil;		/* not reached */
}

static void
rb_struct_modify(VALUE s)
{
    if (OBJ_FROZEN(s)) rb_error_frozen("Struct");
    if (!OBJ_TAINTED(s) && rb_safe_level() >= 4)
       rb_raise(rb_eSecurityError, "Insecure: can't modify Struct");
}

static VALUE
make_struct(VALUE name, VALUE members, VALUE klass)
{
    VALUE nstr;
    ID id;
    long i, len;

    OBJ_FREEZE(members);
    if (NIL_P(name)) {
	nstr = rb_class_new(klass);
#if !WITH_OBJC
	rb_make_metaclass(nstr, RBASIC(klass)->klass);
#endif
	rb_class_inherited(klass, nstr);
    }
    else {
	/* old style: should we warn? */
	name = rb_str_to_str(name);
	id = rb_to_id(name);
	if (!rb_is_const_id(id)) {
	    rb_name_error(id, "identifier %s needs to be constant",
		    StringValuePtr(name));
	}
	if (rb_const_defined_at(klass, id)) {
	    rb_warn("redefining constant Struct::%s", StringValuePtr(name));
	    rb_mod_remove_const(klass, ID2SYM(id));
	}
	nstr = rb_define_class_under(klass, rb_id2name(id), klass);
    }
    rb_ivar_set(nstr, id_members, members);

    rb_objc_define_method(*(VALUE *)nstr, "alloc", struct_alloc, 0);
    rb_objc_define_method(*(VALUE *)nstr, "new", rb_class_new_instance_imp, -1);
    rb_objc_define_method(*(VALUE *)nstr, "[]", rb_class_new_instance_imp, -1);
    rb_objc_define_method(*(VALUE *)nstr, "members", rb_struct_s_members_m, 0);
    len = RARRAY_LEN(members);
    for (i=0; i< len; i++) {
	ID id = SYM2ID(RARRAY_AT(members, i));
	if (rb_is_local_id(id) || rb_is_const_id(id)) {
	    long j = i; /* Needed for block data reference. */
	/* Struct attribute reader */
	rb_objc_define_method(nstr, rb_id2name(id),
		pl_imp_implementationWithBlock(^(VALUE obj) {
		    return RSTRUCT_PTR(obj)[j];
		}), 0);
	/* Struct attribute writer */
	rb_objc_define_method(nstr, rb_id2name(rb_id_attrset(id)),
		pl_imp_implementationWithBlock(^(VALUE obj, VALUE val) {
		    VALUE *ptr = RSTRUCT_PTR(obj);
		    rb_struct_modify(obj);
		    GC_WB(&ptr[i], val);
		    return val;
		}), 1);
	    }
    }

    return nstr;
}

VALUE
rb_struct_alloc_noinit(VALUE klass)
{
    return struct_alloc(klass);
}

VALUE
rb_struct_define_without_accessor(const char *class_name, VALUE super, rb_alloc_func_t alloc, ...)
{
    VALUE klass;
    va_list ar;
    VALUE members;
    long i;
    char *name;

    members = rb_ary_new2(0);
    va_start(ar, alloc);
    i = 0;
    while ((name = va_arg(ar, char*)) != NULL) {
        rb_ary_push(members, ID2SYM(rb_intern(name)));
    }
    va_end(ar);
    OBJ_FREEZE(members);

    if (class_name) {
        klass = rb_define_class(class_name, super);
    }
    else {
	klass = rb_class_new(super);
	rb_make_metaclass(klass, RBASIC(super)->klass);
	rb_class_inherited(super, klass);
    }

    rb_ivar_set(klass, id_members, members);

    rb_objc_define_method(*(VALUE *)klass, "alloc",
	    alloc != NULL ? alloc : struct_alloc,
	    0);

    return klass;
}

VALUE
rb_struct_define(const char *name, ...)
{
    va_list ar;
    VALUE nm, ary;
    char *mem;

    if (!name) nm = Qnil;
    else nm = rb_str_new2(name);
    ary = rb_ary_new();

    va_start(ar, name);
    while ((mem = va_arg(ar, char*)) != 0) {
	ID slot = rb_intern(mem);
	rb_ary_push(ary, ID2SYM(slot));
    }
    va_end(ar);

    return make_struct(nm, ary, rb_cStruct);
}

/*
 *  call-seq:
 *     Struct.new( [aString] [, aSym]+> )    -> StructClass
 *     StructClass.new(arg, ...)             -> obj
 *     StructClass[arg, ...]                 -> obj
 *
 *  Creates a new class, named by <i>aString</i>, containing accessor
 *  methods for the given symbols. If the name <i>aString</i> is
 *  omitted, an anonymous structure class will be created. Otherwise,
 *  the name of this struct will appear as a constant in class
 *  <code>Struct</code>, so it must be unique for all
 *  <code>Struct</code>s in the system and should start with a capital
 *  letter. Assigning a structure class to a constant effectively gives
 *  the class the name of the constant.
 *
 *  <code>Struct::new</code> returns a new <code>Class</code> object,
 *  which can then be used to create specific instances of the new
 *  structure. The number of actual parameters must be
 *  less than or equal to the number of attributes defined for this
 *  class; unset parameters default to <code>nil</code>.  Passing too many
 *  parameters will raise an <code>ArgumentError</code>.
 *
 *  The remaining methods listed in this section (class and instance)
 *  are defined for this generated class.
 *
 *     # Create a structure with a name in Struct
 *     Struct.new("Customer", :name, :address)    #=> Struct::Customer
 *     Struct::Customer.new("Dave", "123 Main")   #=> #<struct Struct::Customer name="Dave", address="123 Main">
 *
 *     # Create a structure named by its constant
 *     Customer = Struct.new(:name, :address)     #=> Customer
 *     Customer.new("Dave", "123 Main")           #=> #<struct Customer name="Dave", address="123 Main">
 */

VALUE rb_mod_module_eval(VALUE mod, SEL sel, int argc, VALUE *argv);

static VALUE
rb_struct_s_def(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE name, rest;
    long i, count;
    VALUE st;
    ID id;

    rb_scan_args(argc, argv, "1*", &name, &rest);
    if (!NIL_P(name) && SYMBOL_P(name)) {
	rb_ary_unshift(rest, name);
	name = Qnil;
    }
    for (i = 0, count = RARRAY_LEN(rest); i < count; i++) {
	id = rb_to_id(RARRAY_AT(rest, i));
	rb_ary_store(rest, i, ID2SYM(id));
    }
    st = make_struct(name, rest, klass);
    if (rb_block_given_p()) {
	rb_mod_module_eval(st, 0, 0, 0);
    }

    return st;
}

static long
num_members(VALUE klass)
{
    VALUE members;
    members = struct_ivar_get(klass, id_members);
    if (TYPE(members) != T_ARRAY) {
	rb_raise(rb_eTypeError, "broken members");
    }
    return RARRAY_LEN(members);
}

/*
 */

VALUE
rb_struct_initialize(VALUE self, SEL sel, VALUE values)
{
    VALUE klass = rb_obj_class(self);
    long n;

    rb_struct_modify(self);
    n = num_members(klass);
    if (n < RARRAY_LEN(values)) {
	rb_raise(rb_eArgError, "struct size differs");
    }
    for (int i = 0; i < RARRAY_LEN(values); i++) {
	GC_WB(&RSTRUCT_PTR(self)[i], RARRAY_AT(values, i));
    }
    if (n > RARRAY_LEN(values)) {
	for (int i = RARRAY_LEN(values); i < n; i++) {
	    RSTRUCT_PTR(self)[i] = Qnil;
	}
    }
    return Qnil;
}

static VALUE
struct_alloc(VALUE klass)
{
    long n;
    NEWOBJ(st, struct RStruct);
    OBJSETUP(st, klass, T_STRUCT);

    n = num_members(klass);

    if (0 < n && n <= RSTRUCT_EMBED_LEN_MAX) {
        RBASIC(st)->flags &= ~RSTRUCT_EMBED_LEN_MASK;
        RBASIC(st)->flags |= n << RSTRUCT_EMBED_LEN_SHIFT;
	rb_mem_clear(st->as.ary, n);
    }
    else {
	if (n > 0) {
	    GC_WB(&st->as.heap.ptr, xmalloc_ptrs(sizeof(VALUE) * n));
	    rb_mem_clear(st->as.heap.ptr, n);
	}
	else {
	    st->as.heap.ptr = NULL;
	}
	st->as.heap.len = n;
    }

    return (VALUE)st;
}

VALUE
rb_struct_alloc(VALUE klass, VALUE values)
{
    return rb_class_new_instance(RARRAY_LEN(values), (VALUE *)RARRAY_PTR(values), klass);
}

VALUE
rb_struct_new(VALUE klass, ...)
{
    VALUE *mem;
    long size, i;
    va_list args;

    size = num_members(klass);
    mem = ALLOCA_N(VALUE, size);
    va_start(args, klass);
    for (i=0; i<size; i++) {
	mem[i] = va_arg(args, VALUE);
    }
    va_end(args);

    return rb_class_new_instance(size, mem, klass);
}

/*
 *  call-seq:
 *     struct.each {|obj| block }  -> struct
 *     struct.each                 -> an_enumerator
 *
 *  Calls <i>block</i> once for each instance variable, passing the
 *  value as a parameter.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     Customer = Struct.new(:name, :address, :zip)
 *     joe = Customer.new("Joe Smith", "123 Maple, Anytown NC", 12345)
 *     joe.each {|x| puts(x) }
 *
 *  <em>produces:</em>
 *
 *     Joe Smith
 *     123 Maple, Anytown NC
 *     12345
 */

static VALUE
rb_struct_each(VALUE s, SEL sel)
{
    long i;

    RETURN_ENUMERATOR(s, 0, 0);
    for (i=0; i<RSTRUCT_LEN(s); i++) {
	rb_yield(RSTRUCT_PTR(s)[i]);
	RETURN_IF_BROKEN();
    }
    return s;
}

/*
 *  call-seq:
 *     struct.each_pair {|sym, obj| block }     -> struct
 *     struct.each_pair                         -> an_enumerator
 *
 *  Calls <i>block</i> once for each instance variable, passing the name
 *  (as a symbol) and the value as parameters.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     Customer = Struct.new(:name, :address, :zip)
 *     joe = Customer.new("Joe Smith", "123 Maple, Anytown NC", 12345)
 *     joe.each_pair {|name, value| puts("#{name} => #{value}") }
 *
 *  <em>produces:</em>
 *
 *     name => Joe Smith
 *     address => 123 Maple, Anytown NC
 *     zip => 12345
 */

static VALUE
rb_struct_each_pair(VALUE s, SEL sel)
{
    VALUE members;
    long i;

    RETURN_ENUMERATOR(s, 0, 0);
    members = rb_struct_members(s);
    for (i=0; i<RSTRUCT_LEN(s); i++) {
	rb_yield_values(2, rb_ary_entry(members, i), RSTRUCT_PTR(s)[i]);
	RETURN_IF_BROKEN();
    }
    return s;
}

static VALUE
inspect_struct(VALUE s, VALUE dummy, int recur)
{
    const char *cname = rb_class2name(rb_obj_class(s));
    VALUE str, members;
    VALUE *ptr;
    long i, len;

    if (recur) {
	return rb_sprintf("#<struct %s:...>", cname);
    }

    members = rb_struct_members(s);
    ptr = RSTRUCT_PTR(s);
    len = RSTRUCT_LEN(s);
    if (cname[0] == '#') {
	str = rb_str_new2("#<struct ");
    }
    else {
	str = rb_sprintf("#<struct %s ", cname);
    }
    for (i=0; i<len; i++) {
	VALUE slot;
	ID id;

	if (i > 0) {
	    rb_str_cat2(str, ", ");
	}
	slot = RARRAY_AT(members, i);
	id = SYM2ID(slot);
	if (rb_is_local_id(id) || rb_is_const_id(id)) {
	    rb_str_buf_append(str, rb_id2str(id));
	}
	else {
	    rb_str_buf_append(str, rb_inspect(slot));
	}
	rb_str_cat2(str, "=");
	rb_str_buf_append(str, rb_inspect(ptr[i]));
    }
    rb_str_cat2(str, ">");
    OBJ_INFECT(str, s);

    return str;
}

/*
 * call-seq:
 *   struct.to_s      -> string
 *   struct.inspect   -> string
 *
 * Describe the contents of this struct in a string.
 */

static VALUE
rb_struct_inspect(VALUE s, SEL sel)
{
    return rb_exec_recursive(inspect_struct, s, 0);
}

/*
 *  call-seq:
 *     struct.to_a     -> array
 *     struct.values   -> array
 *
 *  Returns the values for this instance as an array.
 *
 *     Customer = Struct.new(:name, :address, :zip)
 *     joe = Customer.new("Joe Smith", "123 Maple, Anytown NC", 12345)
 *     joe.to_a[1]   #=> "123 Maple, Anytown NC"
 */

static VALUE
rb_struct_to_a(VALUE s, SEL sel)
{
    return rb_ary_new4(RSTRUCT_LEN(s), RSTRUCT_PTR(s));
}

/* :nodoc: */
VALUE
rb_struct_init_copy(VALUE copy, SEL sel, VALUE s)
{
    if (copy == s) return copy;
    rb_check_frozen(copy);
    if (!rb_obj_is_instance_of(s, rb_obj_class(copy))) {
	rb_raise(rb_eTypeError, "wrong argument class");
    }
    if (RSTRUCT_LEN(copy) != RSTRUCT_LEN(s)) {
	rb_raise(rb_eTypeError, "struct size mismatch");
    }
    for (int i = 0; i < RSTRUCT_LEN(copy); i++) {
	GC_WB(&RSTRUCT_PTR(copy)[i], RSTRUCT_PTR(s)[i]);
    }

    return copy;
}

static VALUE
rb_struct_aref_id(VALUE s, ID id)
{
    VALUE *ptr, members;
    long i, len;

    ptr = RSTRUCT_PTR(s);
    members = rb_struct_members(s);
    len = RARRAY_LEN(members);
    for (i=0; i<len; i++) {
	if (SYM2ID(RARRAY_AT(members, i)) == id) {
	    return ptr[i];
	}
    }
    rb_name_error(id, "no member '%s' in struct", rb_id2name(id));
    return Qnil;		/* not reached */
}

/*
 *  call-seq:
 *     struct[symbol]    -> anObject
 *     struct[fixnum]    -> anObject
 *
 *  Attribute Reference---Returns the value of the instance variable
 *  named by <i>symbol</i>, or indexed (0..length-1) by
 *  <i>fixnum</i>. Will raise <code>NameError</code> if the named
 *  variable does not exist, or <code>IndexError</code> if the index is
 *  out of range.
 *
 *     Customer = Struct.new(:name, :address, :zip)
 *     joe = Customer.new("Joe Smith", "123 Maple, Anytown NC", 12345)
 *
 *     joe["name"]   #=> "Joe Smith"
 *     joe[:name]    #=> "Joe Smith"
 *     joe[0]        #=> "Joe Smith"
 */

static VALUE
rb_struct_aref_imp(VALUE s, SEL sel, VALUE idx)
{
    long i;

    if (TYPE(idx) == T_STRING || TYPE(idx) == T_SYMBOL) {
	return rb_struct_aref_id(s, rb_to_id(idx));
    }

    i = NUM2LONG(idx);
    if (i < 0) i = RSTRUCT_LEN(s) + i;
    if (i < 0)
        rb_raise(rb_eIndexError, "offset %ld too small for struct(size:%ld)",
		 i, RSTRUCT_LEN(s));
    if (RSTRUCT_LEN(s) <= i)
        rb_raise(rb_eIndexError, "offset %ld too large for struct(size:%ld)",
		 i, RSTRUCT_LEN(s));
    return RSTRUCT_PTR(s)[i];
}

VALUE
rb_struct_aref(VALUE s, VALUE idx)
{
    return rb_struct_aref_imp(s, 0, idx);
}

static VALUE
rb_struct_aset_id(VALUE s, ID id, VALUE val)
{
    VALUE members, *ptr;
    long i, len;

    members = rb_struct_members(s);
    len = RARRAY_LEN(members);
    rb_struct_modify(s);
    if (RSTRUCT_LEN(s) != len) {
	rb_raise(rb_eTypeError, "struct size differs (%ld required %ld given)",
		 len, RSTRUCT_LEN(s));
    }
    ptr = RSTRUCT_PTR(s);
    for (i=0; i<len; i++) {
	if (SYM2ID(RARRAY_AT(members, i)) == id) {
	    GC_WB(&ptr[i], val);
	    return val;
	}
    }
    rb_name_error(id, "no member '%s' in struct", rb_id2name(id));
}

/*
 *  call-seq:
 *     struct[symbol] = obj    -> obj
 *     struct[fixnum] = obj    -> obj
 *
 *  Attribute Assignment---Assigns to the instance variable named by
 *  <i>symbol</i> or <i>fixnum</i> the value <i>obj</i> and
 *  returns it. Will raise a <code>NameError</code> if the named
 *  variable does not exist, or an <code>IndexError</code> if the index
 *  is out of range.
 *
 *     Customer = Struct.new(:name, :address, :zip)
 *     joe = Customer.new("Joe Smith", "123 Maple, Anytown NC", 12345)
 *
 *     joe["name"] = "Luke"
 *     joe[:zip]   = "90210"
 *
 *     joe.name   #=> "Luke"
 *     joe.zip    #=> "90210"
 */

static VALUE
rb_struct_aset_imp(VALUE s, SEL sel, VALUE idx, VALUE val)
{
    long i;

    if (TYPE(idx) == T_STRING || TYPE(idx) == T_SYMBOL) {
	return rb_struct_aset_id(s, rb_to_id(idx), val);
    }

    i = NUM2LONG(idx);
    if (i < 0) i = RSTRUCT_LEN(s) + i;
    if (i < 0) {
        rb_raise(rb_eIndexError, "offset %ld too small for struct(size:%ld)",
		 i, RSTRUCT_LEN(s));
    }
    if (RSTRUCT_LEN(s) <= i) {
        rb_raise(rb_eIndexError, "offset %ld too large for struct(size:%ld)",
		 i, RSTRUCT_LEN(s));
    }
    rb_struct_modify(s);
    GC_WB(&RSTRUCT_PTR(s)[i], val);
    return val;
}

VALUE
rb_struct_aset(VALUE s, VALUE idx, VALUE val)
{
    return rb_struct_aset_imp(s, 0, idx, val);
}

static VALUE
struct_entry(VALUE s, long n)
{
    return rb_struct_aref(s, LONG2NUM(n));
}

/*
 * call-seq:
 *   struct.values_at(selector,... )  -> an_array
 *
 *   Returns an array containing the elements in
 *   +self+ corresponding to the given selector(s). The selectors
 *   may be either integer indices or ranges.
 *   See also </code>.select<code>.
 *
 *      a = %w{ a b c d e f }
 *      a.values_at(1, 3, 5)
 *      a.values_at(1, 3, 5, 7)
 *      a.values_at(-1, -3, -5, -7)
 *      a.values_at(1..3, 2...5)
 */

static VALUE
rb_struct_values_at(VALUE s, SEL sel, int argc, VALUE *argv)
{
    return rb_get_values_at(s, RSTRUCT_LEN(s), argc, argv, struct_entry);
}

/*
 *  call-seq:
 *     struct.select {|i| block }    -> array
 *     struct.select                 -> an_enumerator
 *
 *  Invokes the block passing in successive elements from
 *  <i>struct</i>, returning an array containing those elements
 *  for which the block returns a true value (equivalent to
 *  <code>Enumerable#select</code>).
 *
 *     Lots = Struct.new(:a, :b, :c, :d, :e, :f)
 *     l = Lots.new(11, 22, 33, 44, 55, 66)
 *     l.select {|v| (v % 2).zero? }   #=> [22, 44, 66]
 */

static VALUE
rb_struct_select(VALUE s, SEL sel, int argc, VALUE *argv)
{
    VALUE result;
    long i;

    if (argc > 0) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 0)", argc);
    }
    result = rb_ary_new();
    for (i = 0; i < RSTRUCT_LEN(s); i++) {
	VALUE v = rb_yield(RSTRUCT_PTR(s)[i]);
	RETURN_IF_BROKEN();
	if (RTEST(v)) {
	    rb_ary_push(result, RSTRUCT_PTR(s)[i]);
	}
    }

    return result;
}

static VALUE
rb_struct_equal_r(VALUE s, VALUE s2, int recur)
{
    VALUE *ptr, *ptr2;
    long i, len;

    if (recur) {
	return Qtrue;
    }
    ptr = RSTRUCT_PTR(s);
    ptr2 = RSTRUCT_PTR(s2);
    len = RSTRUCT_LEN(s);
    for (i=0; i<len; i++) {
	if (!rb_equal(ptr[i], ptr2[i])) {
	    return Qfalse;
	}
    }
    return Qtrue;
}

/*
 *  call-seq:
 *     struct == other_struct     -> true or false
 *
 *  Equality---Returns <code>true</code> if <i>other_struct</i> is
 *  equal to this one: they must be of the same class as generated by
 *  <code>Struct::new</code>, and the values of all instance variables
 *  must be equal (according to <code>Object#==</code>).
 *
 *     Customer = Struct.new(:name, :address, :zip)
 *     joe   = Customer.new("Joe Smith", "123 Maple, Anytown NC", 12345)
 *     joejr = Customer.new("Joe Smith", "123 Maple, Anytown NC", 12345)
 *     jane  = Customer.new("Jane Doe", "456 Elm, Anytown NC", 12345)
 *     joe == joejr   #=> true
 *     joe == jane    #=> false
 */

static VALUE
rb_struct_equal(VALUE s, SEL sel, VALUE s2)
{
    if (s == s2) {
	return Qtrue;
    }
    if (TYPE(s2) != T_STRUCT) {
	return Qfalse;
    }
    if (rb_obj_class(s) != rb_obj_class(s2)) {
	return Qfalse;
    }
    if (RSTRUCT_LEN(s) != RSTRUCT_LEN(s2)) {
	rb_bug("inconsistent struct"); /* should never happen */
    }
    return rb_exec_recursive(rb_struct_equal_r, s, s2);
}

static VALUE
rb_struct_hash_r(VALUE s, VALUE s2, int recur)
{
    long i, len;
    st_index_t h;
    VALUE n, *ptr;

    h = rb_hash_start(rb_hash(rb_obj_class(s)));
    if (!recur) {
	ptr = RSTRUCT_PTR(s);
	len = RSTRUCT_LEN(s);
	for (i = 0; i < len; i++) {
	    n = rb_hash(ptr[i]);
	    h = rb_hash_uint(h, NUM2LONG(n));
	}
    }
    h = rb_hash_end(h);
    return INT2FIX(h);
}

/*
 * call-seq:
 *   struct.hash   -> fixnum
 *
 * Return a hash value based on this struct's contents.
 */

static VALUE
rb_struct_hash(VALUE s, SEL sel)
{
    return rb_exec_recursive(rb_struct_hash_r, s, Qnil);
}

static VALUE
rb_struct_eql_r(VALUE s, VALUE s2, int recur)
{
    VALUE *ptr, *ptr2;
    long i, len;

    if (recur) {
	return Qtrue;
    }
    ptr = RSTRUCT_PTR(s);
    ptr2 = RSTRUCT_PTR(s2);
    len = RSTRUCT_LEN(s);
    for (i=0; i<len; i++) {
	if (!rb_eql(ptr[i], ptr2[i])) {
	    return Qfalse;
	}
    }
    return Qtrue;
}

/*
 * code-seq:
 *   struct.eql?(other)   -> true or false
 *
 * Two structures are equal if they are the same object, or if all their
 * fields are equal (using <code>eql?</code>).
 */

static VALUE
rb_struct_eql(VALUE s, SEL sel, VALUE s2)
{
    if (s == s2) {
	return Qtrue;
    }
    if (TYPE(s2) != T_STRUCT) {
	return Qfalse;
    }
    if (rb_obj_class(s) != rb_obj_class(s2)) {
	return Qfalse;
    }
    if (RSTRUCT_LEN(s) != RSTRUCT_LEN(s2)) {
	rb_bug("inconsistent struct"); /* should never happen */
    }
    return rb_exec_recursive(rb_struct_eql_r, s, s2);
}

/*
 *  call-seq:
 *     struct.length    -> fixnum
 *     struct.size      -> fixnum
 *
 *  Returns the number of instance variables.
 *
 *     Customer = Struct.new(:name, :address, :zip)
 *     joe = Customer.new("Joe Smith", "123 Maple, Anytown NC", 12345)
 *     joe.length   #=> 3
 */

static VALUE
rb_struct_size(VALUE s, SEL sel)
{
    return LONG2FIX(RSTRUCT_LEN(s));
}

/*
 *  A <code>Struct</code> is a convenient way to bundle a number of
 *  attributes together, using accessor methods, without having to write
 *  an explicit class.
 *
 *  The <code>Struct</code> class is a generator of specific classes,
 *  each one of which is defined to hold a set of variables and their
 *  accessors. In these examples, we'll call the generated class
 *  ``<i>Customer</i>Class,'' and we'll show an example instance of that
 *  class as ``<i>Customer</i>Inst.''
 *
 *  In the descriptions that follow, the parameter <i>symbol</i> refers
 *  to a symbol, which is either a quoted string or a
 *  <code>Symbol</code> (such as <code>:name</code>).
 */
void
Init_Struct(void)
{
    rb_cStruct = rb_define_class("Struct", rb_cObject);
    rb_include_module(rb_cStruct, rb_mEnumerable);

    rb_undef_alloc_func(rb_cStruct);
    rb_objc_define_method(*(VALUE *)rb_cStruct, "new", rb_struct_s_def, -1);

    rb_objc_define_method(rb_cStruct, "initialize", rb_struct_initialize, -2);
    rb_objc_define_method(rb_cStruct, "initialize_copy", rb_struct_init_copy, 1);

    rb_objc_define_method(rb_cStruct, "==", rb_struct_equal, 1);
    rb_objc_define_method(rb_cStruct, "eql?", rb_struct_eql, 1);
    rb_objc_define_method(rb_cStruct, "hash", rb_struct_hash, 0);

    rb_objc_define_method(rb_cStruct, "to_s", rb_struct_inspect, 0);
    rb_objc_define_method(rb_cStruct, "inspect", rb_struct_inspect, 0);
    rb_objc_define_method(rb_cStruct, "to_a", rb_struct_to_a, 0);
    rb_objc_define_method(rb_cStruct, "values", rb_struct_to_a, 0);
    rb_objc_define_method(rb_cStruct, "size", rb_struct_size, 0);
    rb_objc_define_method(rb_cStruct, "length", rb_struct_size, 0);

    rb_objc_define_method(rb_cStruct, "each", rb_struct_each, 0);
    rb_objc_define_method(rb_cStruct, "each_pair", rb_struct_each_pair, 0);
    rb_objc_define_method(rb_cStruct, "[]", rb_struct_aref_imp, 1);
    rb_objc_define_method(rb_cStruct, "[]=", rb_struct_aset_imp, 2);
    rb_objc_define_method(rb_cStruct, "select", rb_struct_select, -1);
    rb_objc_define_method(rb_cStruct, "values_at", rb_struct_values_at, -1);

    rb_objc_define_method(rb_cStruct, "members", rb_struct_members_m, 0);
    id_members = rb_intern("__members__");
}
