/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 2001-2003 Akinori MUSHA
 */

#include "macruby_internal.h"
#include "id.h"
#include "ruby/node.h"
#include "vm.h"

/*
 * Document-class: Enumerator
 *
 * A class which provides a method `each' to be used as an Enumerable
 * object.
 *
 * An enumerator can be created by following methods.
 * - Kernel#to_enum
 * - Kernel#enum_for
 * - Enumerator.new
 *
 * Also, most iteration methods without a block returns an enumerator.
 * For example, Array#map returns an enumerator if a block is not given.
 * The enumerator has the with_index method.
 * So ary.map.with_index works as follows.
 *
 *   p %w[foo bar baz].map.with_index {|w,i| "#{i}:#{w}" }
 *   #=> ["0:foo", "1:bar", "2:baz"]
 *
 * An enumerator object can be used as an external iterator.
 * I.e.  Enumerator#next returns the next value of the iterator.
 * Enumerator#next raises StopIteration at end.
 *
 *   e = [1,2,3].each   # returns an enumerator object.
 *   p e.next   #=> 1
 *   p e.next   #=> 2
 *   p e.next   #=> 3
 *   p e.next   #raises StopIteration
 *
 * An external iterator can be used to implement an internal iterator as follows.
 *
 *   def ext_each(e)
 *     while true
 *       begin
 *         vs = e.next_values
 *       rescue StopIteration
 *         return $!.result
 *       end
 *       y = yield(*vs)
 *       e.feed y
 *     end
 *   end
 *
 *   o = Object.new
 *   def o.each
 *     p yield
 *     p yield(1)
 *     p yield(1, 2)
 *     3
 *   end
 *
 *   # use o.each as an internal iterator directly.
 *   p o.each {|*x| p x; [:b, *x] }
 *   #=> [], [:b], [1], [:b, 1], [1, 2], [:b, 1, 2], 3
 *
 *   # convert o.each to an external iterator for
 *   # implementing an internal iterator.
 *   p ext_each(o.to_enum) {|*x| p x; [:b, *x] }
 *   #=> [], [:b], [1], [:b, 1], [1, 2], [:b, 1, 2], 3
 *
 */
VALUE rb_cEnumerator;
static VALUE sym_each;

VALUE rb_eStopIteration;

struct enumerator {
    VALUE obj;
    SEL   sel;
    VALUE args;
    VALUE fib;
    VALUE dst;
    VALUE no_next;
};

static VALUE rb_cGenerator, rb_cYielder;

struct generator {
    VALUE proc;
};

struct yielder {
    VALUE proc;
};

static VALUE generator_allocate(VALUE klass, SEL sel);
static VALUE generator_init(VALUE obj, VALUE proc);

static struct enumerator *
enumerator_ptr(VALUE obj)
{
    struct enumerator *ptr;

    Data_Get_Struct(obj, struct enumerator, ptr);
#if 0
    if (RDATA(obj)->dmark != enumerator_mark) {
	rb_raise(rb_eTypeError,
		 "wrong argument type %s (expected %s)",
		 rb_obj_classname(obj), rb_class2name(rb_cEnumerator));
    }
#endif
    if (!ptr || ptr->obj == Qundef) {
	rb_raise(rb_eArgError, "uninitialized enumerator");
    }
    return ptr;
}

/*
 *  call-seq:
 *    obj.to_enum(method = :each, *args)
 *    obj.enum_for(method = :each, *args)
 *
 *  Returns Enumerator.new(self, method, *args).
 *
 *  e.g.:
 *
 *     str = "xyz"
 *
 *     enum = str.enum_for(:each_byte)
 *     a = enum.map {|b| '%02x' % b } #=> ["78", "79", "7a"]
 *
 *     # protects an array from being modified
 *     a = [1, 2, 3]
 *     some_method(a.to_enum)
 *
 */
static VALUE
obj_to_enum(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE meth = sym_each;

    if (argc > 0) {
	--argc;
	meth = *argv++;
    }

    ID meth_id = rb_to_id(meth);
    SEL enum_sel = rb_vm_id_to_sel(meth_id, argc);
    return rb_enumeratorize(obj, enum_sel, argc, argv);
}

static VALUE
enumerator_allocate(VALUE klass, SEL sel)
{
    struct enumerator *ptr;
    VALUE enum_obj;

    enum_obj = Data_Make_Struct(klass, struct enumerator,
				NULL, NULL, ptr);
    ptr->obj = Qundef;

    return enum_obj;
}

static VALUE
enumerator_each_i(VALUE v, VALUE enum_obj, int argc, VALUE *argv)
{
    return rb_yield_values2(argc, argv);
}

static VALUE
enumerator_init(VALUE enum_obj, VALUE obj, SEL sel, int argc, VALUE *argv)
{
    struct enumerator *ptr;

    Data_Get_Struct(enum_obj, struct enumerator, ptr);

    if (!ptr) {
	rb_raise(rb_eArgError, "unallocated enumerator");
    }

    GC_WB(&ptr->obj, obj);
    ptr->sel = sel;
    if (argc > 0) {
	GC_WB(&ptr->args, rb_ary_new4(argc, argv));
    }
    ptr->fib = 0;
    ptr->dst = Qnil;
    ptr->no_next = Qfalse;

    return enum_obj;
}

/*
 *  call-seq:
 *    Enumerator.new(obj, method = :each, *args)
 *    Enumerator.new { |y| ... }
 *
 *  Creates a new Enumerator object, which is to be used as an
 *  Enumerable object iterating in a given way.
 *
 *  In the first form, a generated Enumerator iterates over the given
 *  object using the given method with the given arguments passed.
 *  Use of this form is discouraged.  Use Kernel#enum_for(), alias
 *  to_enum, instead.
 *
 *    e = Enumerator.new(ObjectSpace, :each_object)
 *        #-> ObjectSpace.enum_for(:each_object)
 *
 *    e.select { |obj| obj.is_a?(Class) }  #=> array of all classes
 *
 *  In the second form, iteration is defined by the given block, in
 *  which a "yielder" object given as block parameter can be used to
 *  yield a value by calling the +yield+ method, alias +<<+.
 *
 *    fib = Enumerator.new { |y|
 *      a = b = 1
 *      loop {
 *        y << a
 *        a, b = b, a + b
 *      }
 *    }
 *
 *    p fib.take(10) #=> [1, 1, 2, 3, 5, 8, 13, 21, 34, 55]
 */
static VALUE
enumerator_initialize(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE recv, meth = sym_each;

    if (argc == 0) {
	if (!rb_block_given_p())
	    rb_raise(rb_eArgError, "wrong number of argument (0 for 1+)");

	recv = generator_init(generator_allocate(rb_cGenerator, 0), rb_block_proc());
    }
    else {
	recv = *argv++;
	if (--argc) {
	    meth = *argv++;
	    --argc;
	}
    }

    ID meth_id = rb_to_id(meth);
    SEL meth_sel = rb_vm_id_to_sel(meth_id, argc);
    return enumerator_init(obj, recv, meth_sel, argc, argv);
}

/* :nodoc: */
static VALUE
enumerator_init_copy(VALUE obj, SEL sel, VALUE orig)
{
    struct enumerator *ptr0, *ptr1;

    ptr0 = enumerator_ptr(orig);
    if (ptr0->fib) {
	/* Fibers cannot be copied */
	rb_raise(rb_eTypeError, "can't copy execution context");
    }

    Data_Get_Struct(obj, struct enumerator, ptr1);

    if (!ptr1) {
	rb_raise(rb_eArgError, "unallocated enumerator");
    }

    GC_WB(&ptr1->obj, ptr0->obj);
    ptr1->sel = ptr0->sel;
    if (ptr0->args != 0) {
	GC_WB(&ptr1->args, ptr0->args);
    }
    ptr1->fib  = 0;

    return obj;
}

VALUE
rb_enumeratorize(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    return enumerator_init(enumerator_allocate(rb_cEnumerator, 0), obj, sel,
	    argc, argv);
}

static VALUE
enumerator_block_call(VALUE obj, VALUE (*func)(ANYARGS), VALUE arg)
{
    struct enumerator *e;
    int argc = 0;
    const VALUE *argv = 0;

    e = enumerator_ptr(obj);
    if (e->args != 0) {
	argc = RARRAY_LENINT(e->args);
	argv = RARRAY_PTR(e->args);
    }
    return rb_objc_block_call(e->obj, e->sel, argc, (VALUE *)argv,
	    func, arg);
}

/*
 *  call-seq:
 *    enum.each {...}
 *
 *  Iterates the given block using the object and the method specified
 *  in the first place.  If no block is given, returns self.
 *
 */
static VALUE
enumerator_each(VALUE obj, SEL sel)
{
    if (!rb_block_given_p()) {
	return obj;
    }
    return enumerator_block_call(obj, enumerator_each_i, obj);
}

static VALUE
enumerator_with_index_i(VALUE val, VALUE m, int argc, VALUE *argv)
{
    VALUE idx;
    VALUE *memo = (VALUE *)m;

    idx = INT2FIX(*memo);
    ++*memo;

    if (argc <= 1)
	return rb_yield_values(2, val, idx);

    return rb_yield_values(2, rb_ary_new4(argc, argv), idx);
}

/*
 *  call-seq:
 *    e.with_index(offset = 0) {|(*args), idx| ... }
 *    e.with_index(offset = 0)
 *
 *  Iterates the given block for each element with an index, which
 *  starts from +offset+.  If no block is given, returns an enumerator.
 *
 */
static VALUE
enumerator_with_index(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE memo;

    rb_scan_args(argc, argv, "01", &memo);
    RETURN_ENUMERATOR(obj, argc, argv);
    memo = NIL_P(memo) ? 0 : (VALUE)NUM2LONG(memo);
    return enumerator_block_call(obj, enumerator_with_index_i, (VALUE)&memo);
}

/*
 *  call-seq:
 *    e.each_with_index {|(*args), idx| ... }
 *    e.each_with_index
 *
 *  Same as Enumerator#with_index, except each_with_index does not
 *  receive an offset argument.
 *
 */
static VALUE
enumerator_each_with_index(VALUE obj, SEL sel)
{
    return enumerator_with_index(obj, sel, 0, NULL);
}

static VALUE
enumerator_with_object_i(VALUE val, VALUE memo, int argc, VALUE *argv)
{
    if (argc <= 1) {
	return rb_yield_values(2, val, memo);
    }

    return rb_yield_values(2, rb_ary_new4(argc, argv), memo);
}

/*
 *  call-seq:
 *    e.with_object(obj) {|(*args), memo_obj| ... }
 *    e.with_object(obj)
 *
 *  Iterates the given block for each element with an arbitrary
 *  object given, and returns the initially given object.
 *
 *  If no block is given, returns an enumerator.
 *
 */
static VALUE
enumerator_with_object(VALUE obj, SEL sel, VALUE memo)
{
    RETURN_ENUMERATOR(obj, 1, &memo);
    enumerator_block_call(obj, enumerator_with_object_i, memo);
    return memo;
}

#if 0
static VALUE
next_ii(VALUE i, VALUE obj, int argc, VALUE *argv)
{
    rb_fiber_yield(argc, argv);
    return Qnil;
}

static VALUE
next_i(VALUE curr, VALUE obj)
{
    struct enumerator *e = enumerator_ptr(obj);
    VALUE rnil = Qnil;

    rb_block_call(obj, rb_intern("each"), 0, 0, next_ii, obj);
    e->no_next = Qtrue;
    return rb_fiber_yield(1, &rnil);
}

static void
next_init(VALUE obj, struct enumerator *e)
{
    VALUE curr = rb_fiber_current();
    e->dst = curr;
    e->fib = rb_fiber_new(next_i, obj);
}
#endif

/*
 * call-seq:
 *   e.next   -> object
 *
 * Returns the next object in the enumerator, and move the internal
 * position forward.  When the position reached at the end, StopIteration
 * is raised.
 *
 *   a = [1,2,3]
 *   e = a.to_enum
 *   p e.next   #=> 1
 *   p e.next   #=> 2
 *   p e.next   #=> 3
 *   p e.next   #raises StopIteration
 *
 * Note that enumeration sequence by next method does not affect other
 * non-external enumeration methods, unless underlying iteration
 * methods itself has side-effect, e.g. IO#each_line.
 *
 */

static VALUE
enumerator_next(VALUE obj, SEL sel)
{
    // TODO
#if 0
    struct enumerator *e = enumerator_ptr(obj);
    VALUE curr, v;
    curr = rb_fiber_current();

    if (!e->fib || !rb_fiber_alive_p(e->fib)) {
	next_init(obj, e);
    }

    v = rb_fiber_resume(e->fib, 1, &curr);
    if (e->no_next) {
	e->fib = 0;
	e->dst = Qnil;
	e->no_next = Qfalse;
	rb_raise(rb_eStopIteration, "iteration reached at end");
    }
    return v;
#endif
    return Qnil;
}

/*
 * call-seq:
 *   e.rewind   -> e
 *
 * Rewinds the enumeration sequence by the next method.
 *
 * If the enclosed object responds to a "rewind" method, it is called.
 */

static VALUE
enumerator_rewind(VALUE obj, SEL sel)
{
    struct enumerator *e = enumerator_ptr(obj);

    e->fib = 0;
    e->dst = Qnil;
    e->no_next = Qfalse;
    return obj;
}

static VALUE
inspect_enumerator(VALUE obj, VALUE dummy, int recur)
{
    struct enumerator *e;
    const char *cname;
    VALUE eobj, str;
    int tainted, untrusted;

    Data_Get_Struct(obj, struct enumerator, e);

    cname = rb_obj_classname(obj);

    if (!e || e->obj == Qundef) {
	return rb_sprintf("#<%s: uninitialized>", cname);
    }

    if (recur) {
	str = rb_sprintf("#<%s: ...>", cname);
	OBJ_TAINT(str);
	return str;
    }

    eobj = e->obj;

    tainted   = OBJ_TAINTED(eobj);
    untrusted = OBJ_UNTRUSTED(eobj);

    /* (1..100).each_cons(2) => "#<Enumerator: 1..100:each_cons(2)>" */
    str = rb_sprintf("#<%s: ", cname);
    rb_str_concat(str, rb_inspect(eobj));
    rb_str_buf_cat2(str, ":");
    const char *method_name = sel_getName(e->sel);
    long length = strlen(method_name);
    if (method_name[length-1] == ':') {
	length--;
    }
    rb_str_buf_cat(str, method_name, length);

    if (e->args) {
	long   argc = RARRAY_LEN(e->args);
	VALUE *argv = (VALUE*)RARRAY_PTR(e->args);

	rb_str_buf_cat2(str, "(");

	while (argc--) {
	    VALUE arg = *argv++;

	    rb_str_concat(str, rb_inspect(arg));
	    rb_str_buf_cat2(str, argc > 0 ? ", " : ")");

	    if (OBJ_TAINTED(arg)) tainted = TRUE;
	    if (OBJ_UNTRUSTED(arg)) untrusted = TRUE;
	}
    }

    rb_str_buf_cat2(str, ">");

    if (tainted) OBJ_TAINT(str);
    if (untrusted) OBJ_UNTRUST(str);
    return str;
}

/*
 * call-seq:
 *   e.inspect  -> string
 *
 *  Create a printable version of <i>e</i>.
 */

static VALUE
enumerator_inspect(VALUE obj, SEL sel)
{
    return rb_exec_recursive(inspect_enumerator, obj, 0);
}

/*
 * Yielder
 */
#if !WITH_OBJC
static void
yielder_mark(void *p)
{
    struct yielder *ptr = p;
    rb_gc_mark(ptr->proc);
}
#endif

static struct yielder *
yielder_ptr(VALUE obj)
{
    struct yielder *ptr;

    Data_Get_Struct(obj, struct yielder, ptr);
#if !WITH_OBJC
    if (RDATA(obj)->dmark != yielder_mark) {
	rb_raise(rb_eTypeError,
		 "wrong argument type %s (expected %s)",
		 rb_obj_classname(obj), rb_class2name(rb_cYielder));
    }
#endif
    if (!ptr || ptr->proc == Qundef) {
	rb_raise(rb_eArgError, "uninitialized yielder");
    }
    return ptr;
}

/* :nodoc: */
static VALUE
yielder_allocate(VALUE klass, SEL sel)
{
    struct yielder *ptr;
    VALUE obj;

    obj = Data_Make_Struct(klass, struct yielder, NULL, NULL, ptr);
    ptr->proc = Qundef;

    return obj;
}

static VALUE
yielder_init(VALUE obj, VALUE proc)
{
    struct yielder *ptr;

    Data_Get_Struct(obj, struct yielder, ptr);

    if (!ptr) {
	rb_raise(rb_eArgError, "unallocated yielder");
    }

    GC_WB(&ptr->proc, proc);

    return obj;
}

/* :nodoc: */
static VALUE
yielder_initialize(VALUE obj, SEL sel)
{
    rb_need_block();

    return yielder_init(obj, rb_block_proc());
}

/* :nodoc: */
static VALUE
yielder_yield(VALUE obj, SEL sel, VALUE args)
{
    struct yielder *ptr = yielder_ptr(obj);

    return rb_proc_call(ptr->proc, args);
}

/* :nodoc: */
static VALUE yielder_yield_push(VALUE obj, SEL sel, VALUE args)
{
    yielder_yield(obj, 0, args);
    return obj;
}

static VALUE
yielder_yield_i(VALUE obj, VALUE memo, int argc, VALUE *argv)
{
    return rb_yield_values2(argc, argv);
}

static VALUE
yielder_new(void)
{
    return yielder_init(yielder_allocate(rb_cYielder, 0), rb_proc_new(yielder_yield_i, 0));
}

/*
 * Generator
 */
#if !WITH_OBJC
static void
generator_mark(void *p)
{
    struct generator *ptr = p;
    rb_gc_mark(ptr->proc);
}
#endif

static struct generator *
generator_ptr(VALUE obj)
{
    struct generator *ptr;

    Data_Get_Struct(obj, struct generator, ptr);
#if !WITH_OBJC
    if (RDATA(obj)->dmark != generator_mark) {
	rb_raise(rb_eTypeError,
		 "wrong argument type %s (expected %s)",
		 rb_obj_classname(obj), rb_class2name(rb_cGenerator));
    }
#endif
    if (!ptr || ptr->proc == Qundef) {
	rb_raise(rb_eArgError, "uninitialized generator");
    }
    return ptr;
}

/* :nodoc: */
static VALUE
generator_allocate(VALUE klass, SEL sel)
{
    struct generator *ptr;
    VALUE obj;

    obj = Data_Make_Struct(klass, struct generator, NULL, NULL, ptr);
    ptr->proc = Qundef;

    return obj;
}

static VALUE
generator_init(VALUE obj, VALUE proc)
{
    struct generator *ptr;

    Data_Get_Struct(obj, struct generator, ptr);

    if (!ptr) {
	rb_raise(rb_eArgError, "unallocated generator");
    }

    GC_WB(&ptr->proc, proc);

    return obj;
}

VALUE rb_obj_is_proc(VALUE proc);

/* :nodoc: */
static VALUE
generator_initialize(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE proc;

    if (argc == 0) {
	rb_need_block();

	proc = rb_block_proc();
    } else {
	rb_scan_args(argc, argv, "1", &proc);

	if (!rb_obj_is_proc(proc))
	    rb_raise(rb_eTypeError,
		     "wrong argument type %s (expected Proc)",
		     rb_obj_classname(proc));

	if (rb_block_given_p()) {
	    rb_warn("given block not used");
	}
    }

    return generator_init(obj, proc);
}

/* :nodoc: */
static VALUE
generator_init_copy(VALUE obj, SEL sel, VALUE orig)
{
    struct generator *ptr0, *ptr1;

    ptr0 = generator_ptr(orig);

    Data_Get_Struct(obj, struct generator, ptr1);

    if (!ptr1) {
	rb_raise(rb_eArgError, "unallocated generator");
    }

    ptr1->proc = ptr0->proc;

    return obj;
}

/* :nodoc: */
static VALUE
generator_each(VALUE obj, SEL sel)
{
    struct generator *ptr = generator_ptr(obj);
    VALUE yielder;

    yielder = yielder_new();

    return rb_proc_call(ptr->proc, rb_ary_new3(1, yielder));

    return obj;
}

void
Init_Enumerator(void)
{
    rb_objc_define_method(rb_mKernel, "to_enum", obj_to_enum, -1);
    rb_objc_define_method(rb_mKernel, "enum_for", obj_to_enum, -1);

    rb_cEnumerator = rb_define_class("Enumerator", rb_cObject);
    rb_include_module(rb_cEnumerator, rb_mEnumerable);

    rb_objc_define_method(*(VALUE *)rb_cEnumerator, "alloc", enumerator_allocate, 0);
    rb_objc_define_method(rb_cEnumerator, "initialize", enumerator_initialize, -1);
    rb_objc_define_method(rb_cEnumerator, "initialize_copy", enumerator_init_copy, 1);
    rb_objc_define_method(rb_cEnumerator, "each", enumerator_each, 0);
    rb_objc_define_method(rb_cEnumerator, "each_with_index", enumerator_each_with_index, 0);
    rb_objc_define_method(rb_cEnumerator, "each_with_object", enumerator_with_object, 1);
    rb_objc_define_method(rb_cEnumerator, "with_index", enumerator_with_index, -1);
    rb_objc_define_method(rb_cEnumerator, "with_object", enumerator_with_object, 1);
    rb_objc_define_method(rb_cEnumerator, "next", enumerator_next, 0);
    rb_objc_define_method(rb_cEnumerator, "rewind", enumerator_rewind, 0);
    rb_objc_define_method(rb_cEnumerator, "inspect", enumerator_inspect, 0);

    rb_eStopIteration   = rb_define_class("StopIteration", rb_eIndexError);

    /* Generator */
    rb_cGenerator = rb_define_class_under(rb_cEnumerator, "Generator", rb_cObject);
    rb_include_module(rb_cGenerator, rb_mEnumerable);
    rb_objc_define_method(*(VALUE *)rb_cGenerator, "alloc", generator_allocate, 0);
    rb_objc_define_method(rb_cGenerator, "initialize", generator_initialize, -1);
    rb_objc_define_method(rb_cGenerator, "initialize_copy", generator_init_copy, 1);
    rb_objc_define_method(rb_cGenerator, "each", generator_each, 0);

    /* Yielder */
    rb_cYielder = rb_define_class_under(rb_cEnumerator, "Yielder", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cYielder, "alloc", yielder_allocate, 0);
    rb_objc_define_method(rb_cYielder, "initialize", yielder_initialize, 0);
    rb_objc_define_method(rb_cYielder, "yield", yielder_yield, -2);
    rb_objc_define_method(rb_cYielder, "<<", yielder_yield_push, -2);

    sym_each	 	= ID2SYM(rb_intern("each"));
}
