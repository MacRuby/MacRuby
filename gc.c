/* 
 * MacRuby implementation of Ruby 1.9's gc.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2008, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include "ruby/ruby.h"
#include "ruby/signal.h"
#include "ruby/st.h"
#include "ruby/node.h"
#include "ruby/re.h"
#include "ruby/io.h"
#include "ruby/util.h"
#include "objc.h"
#include "roxor.h"
#include <stdio.h>
#include <setjmp.h>
#include <sys/types.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#include <mach/mach.h>
#if HAVE_AUTO_ZONE_H
# include <auto_zone.h>
#else
# include "auto_zone.h"
#endif
static auto_zone_t *__auto_zone = NULL;

int rb_io_fptr_finalize(struct rb_io_t *io_struct);

static VALUE nomem_error;

static bool dont_gc = false;

void
rb_global_variable(VALUE *var)
{
    rb_gc_register_address(var);
}

void
rb_memerror(void)
{
    rb_exc_raise(nomem_error);
}

/*
 *  call-seq:
 *    GC.stress                 => true or false
 *
 *  returns current status of GC stress mode.
 */

static VALUE
gc_stress_get(VALUE self, SEL sel)
{
    rb_notimplement();
    return Qnil;
}

/*
 *  call-seq:
 *    GC.stress = bool          => bool
 *
 *  updates GC stress mode.
 *
 *  When GC.stress = true, GC is invoked for all GC opportunity:
 *  all memory and object allocation.
 *
 *  Since it makes Ruby very slow, it is only for debugging.
 */

static VALUE
gc_stress_set(VALUE self, SEL sel, VALUE flag)
{
    rb_notimplement();
    return Qnil;
}

static int garbage_collect(void);

static void
rb_objc_no_gc_error(void)
{ 
    fprintf(stderr,
	    "The client that links against MacRuby was not built for "\
	    "GC. Please turn on garbage collection (-fobjc-gc) and "\
	    "try again.\n");
    exit(1);
}

void *
ruby_xmalloc(size_t size)
{
    void *mem;

    if (size < 0) {
	rb_raise(rb_eNoMemError, "negative allocation size (or too big)");
    }
    if (size == 0) {
	size = 1;
    }
    if (__auto_zone == NULL) {
	rb_objc_no_gc_error();
    }

    mem = auto_zone_allocate_object(__auto_zone, size, 
				    AUTO_MEMORY_SCANNED, 0, 0);
    if (mem == NULL) {
	rb_memerror();
    }

    return mem;
}

void *
ruby_xmalloc2(size_t n, size_t size)
{
    size_t len = size * n;
    if (n != 0 && size != len / n) {
	rb_raise(rb_eArgError, "malloc: possible integer overflow");
    }
    return ruby_xmalloc(len);
}

void *
ruby_xcalloc(size_t n, size_t size)
{
    void *mem;

    mem = ruby_xmalloc2(n, size);
    memset(mem, 0, n * size);

    return mem;
}


void *
ruby_xrealloc(void *ptr, size_t size)
{
    void *mem;

    if (size < 0) {
	rb_raise(rb_eArgError, "negative re-allocation size");
    }
    if (ptr == NULL) {
	return ruby_xmalloc(size);
    }
    if (size == 0) {
	size = 1;
    }
    
    mem = malloc_zone_realloc(__auto_zone, ptr, size);

    if (mem == NULL) {
	rb_memerror();
    }

    return mem;
}

void *
ruby_xrealloc2(void *ptr, size_t n, size_t size)
{
    size_t len = size * n;
    if (n != 0 && size != len / n) {
	rb_raise(rb_eArgError, "realloc: possible integer overflow");
    }
    return ruby_xrealloc(ptr, len);
}

void
ruby_xfree(void *ptr)
{
    if (ptr != NULL) {
	auto_zone_retain(__auto_zone, ptr);
	malloc_zone_free(__auto_zone, ptr);
    }
}


/*
 *  call-seq:
 *     GC.enable    => true or false
 *
 *  Enables garbage collection, returning <code>true</code> if garbage
 *  collection was previously disabled.
 *
 *     GC.disable   #=> false
 *     GC.enable    #=> true
 *     GC.enable    #=> false
 *
 */

VALUE
rb_gc_enable(VALUE self, SEL sel)
{
    int old = dont_gc;

    auto_collector_reenable(__auto_zone);
    dont_gc = Qfalse;
    return old;
}

/*
 *  call-seq:
 *     GC.disable    => true or false
 *
 *  Disables garbage collection, returning <code>true</code> if garbage
 *  collection was already disabled.
 *
 *     GC.disable   #=> false
 *     GC.disable   #=> true
 *
 */

VALUE
rb_gc_disable(VALUE self, SEL sel)
{
    int old = dont_gc;

    auto_collector_disable(__auto_zone);
    dont_gc = Qtrue;
    return old;
}

VALUE rb_mGC;

void
rb_gc_assign_weak_ref(const void *value, void *const*location)
{
    auto_assign_weak_reference(__auto_zone, value, location, NULL);
}

void*
rb_gc_read_weak_ref(void **referrer)
{
    return auto_read_weak_reference(__auto_zone, referrer);
}


void
rb_objc_wb(void *dst, void *newval)
{
    if (!SPECIAL_CONST_P(newval)) {
	if (!auto_zone_set_write_barrier(__auto_zone, dst, newval)) {
	    rb_bug("destination %p isn't in the auto zone", dst);
	}
    }
    *(void **)dst = newval;
}

void *
rb_gc_memmove(void *dst, const void *src, size_t len)
{
    return auto_zone_write_barrier_memmove(__auto_zone, dst, src, len);
}

void
rb_objc_root(void *addr)
{
    if (addr != NULL) {
	auto_zone_add_root(__auto_zone, addr, *(void **)addr);
    }
}

void
rb_objc_retain(const void *addr)
{
    if (addr != NULL && !SPECIAL_CONST_P(addr)) {
	auto_zone_retain(__auto_zone, (void *)addr);
    }
}

void
rb_objc_release(const void *addr)
{
    if (addr != NULL && !SPECIAL_CONST_P(addr)) {
	auto_zone_release(__auto_zone, (void *)addr);
    }
}

void
rb_objc_set_associative_ref(void *obj, void *key, void *val)
{
    auto_zone_set_associative_ref(__auto_zone, obj, key, val);
}

void *
rb_objc_get_associative_ref(void *obj, void *key)
{
    return auto_zone_get_associative_ref(__auto_zone, obj, key);
}

void
rb_gc_register_address(VALUE *addr)
{
    rb_objc_root(addr);
}

void
rb_register_mark_object(VALUE obj)
{
    rb_gc_register_address(&obj);
}

void
rb_gc_unregister_address(VALUE *addr)
{
    /* TODO: implement me */
}

static void *__nsobject = NULL;

void *
rb_objc_newobj(size_t size)
{
    void *obj;

    obj = auto_zone_allocate_object(__auto_zone, size, AUTO_OBJECT_SCANNED, 0, 0);
    assert(obj != NULL);
    RBASIC(obj)->klass = (VALUE)__nsobject;
    return obj;
}

void
rb_objc_gc_register_thread(void)
{
    auto_zone_register_thread(__auto_zone);
}

void
rb_objc_gc_unregister_thread(void)
{
    auto_zone_unregister_thread(__auto_zone);
}

NODE*
rb_node_newnode(enum node_type type, VALUE a0, VALUE a1, VALUE a2)
{
    NODE *n = xmalloc(sizeof(struct RNode));

    n->flags |= T_NODE;
    nd_set_type(n, type);

    GC_WB(&n->u1.value, a0);
    GC_WB(&n->u2.value, a1);
    GC_WB(&n->u3.value, a2);

    return n;
}

VALUE
rb_data_object_alloc(VALUE klass, void *datap, RUBY_DATA_FUNC dmark, RUBY_DATA_FUNC dfree)
{
    NEWOBJ(data, struct RData);
    if (klass) Check_Type(klass, T_CLASS);
    OBJSETUP(data, klass, T_DATA);
    GC_WB(&data->data, datap);
    data->dfree = dfree;
    data->dmark = dmark;

    return (VALUE)data;
}

void
rb_gc_force_recycle(VALUE p)
{
    xfree((void *)p);
}

static int
garbage_collect(void)
{
    if (dont_gc)
	return Qtrue;
    auto_collect(__auto_zone, AUTO_COLLECT_GENERATIONAL_COLLECTION, NULL);
    return Qtrue;
}

void
rb_gc(void)
{
    garbage_collect();
}

/*
 *  call-seq:
 *     GC.start                     => nil
 *     gc.garbage_collect           => nil
 *     ObjectSpace.garbage_collect  => nil
 *
 *  Initiates garbage collection, unless manually disabled.
 *
 */

VALUE
rb_gc_start(VALUE self, SEL sel)
{
    rb_gc();
    return Qnil;
}

/*
 * Document-class: ObjectSpace
 *
 *  The <code>ObjectSpace</code> module contains a number of routines
 *  that interact with the garbage collection facility and allow you to
 *  traverse all living objects with an iterator.
 *
 *  <code>ObjectSpace</code> also provides support for object
 *  finalizers, procs that will be called when a specific object is
 *  about to be destroyed by garbage collection.
 *
 *     include ObjectSpace
 *
 *
 *     a = "A"
 *     b = "B"
 *     c = "C"
 *
 *
 *     define_finalizer(a, proc {|id| puts "Finalizer one on #{id}" })
 *     define_finalizer(a, proc {|id| puts "Finalizer two on #{id}" })
 *     define_finalizer(b, proc {|id| puts "Finalizer three on #{id}" })
 *
 *  <em>produces:</em>
 *
 *     Finalizer three on 537763470
 *     Finalizer one on 537763480
 *     Finalizer two on 537763480
 *
 */

struct rb_objc_recorder_context {
    VALUE class_of;
    int count;
    VALUE break_value;
};

static int
rb_objc_yield_classes(VALUE of)
{
    int i, count, rcount;
    Class *buf;

    count = objc_getClassList(NULL, 0);
    assert(count > 0);

    buf = (Class *)alloca(sizeof(Class) * count);
    objc_getClassList(buf, count);

    for (i = rcount = 0; i < count; i++) {
	Class sk, k = buf[i];
	bool nsobject_based;

	if (class_getName(k)[0] == '_')
	    continue;

	if (of == rb_cModule && !RCLASS_MODULE(k))
	    continue;

	nsobject_based = false;
	sk = k;
	do {
	    sk = (Class)RCLASS_SUPER(sk);
	    if (sk == (Class)rb_cNSObject) {
		nsobject_based = true;
		break;
	    }
	}
	while (sk != NULL);	

	if (nsobject_based) {
	    rb_yield((VALUE)k);
	    RETURN_IF_BROKEN();
	    rcount++;
	}
    }

    return rcount;
}

static void 
rb_objc_recorder(task_t task, void *context, unsigned type_mask,
		 vm_range_t *ranges, unsigned range_count)
{
    struct rb_objc_recorder_context *ctx;
    vm_range_t *r, *end;

    ctx = (struct rb_objc_recorder_context *)context;

    for (r = ranges, end = ranges + range_count; r < end; r++) {
	Class c;
	auto_memory_type_t type =
	    auto_zone_get_layout_type(__auto_zone, (void *)r->address);
	if (type != AUTO_OBJECT_SCANNED && type != AUTO_OBJECT_UNSCANNED)
	    continue;
	if (*(Class *)r->address == NULL)
	    continue;
	if (ctx->class_of != 0) {
	    bool ok = false;
	    for (c = *(Class *)r->address; c != NULL; 
		    c = class_getSuperclass(c)) {
		if (c ==(Class)ctx->class_of) {
		    ok = true;
		    break;
		}
	    }
	    if (!ok)
		continue;
	}
	switch (TYPE(r->address)) {
	    case T_NONE: 
	    case T_NODE:
		continue;
	    case T_ICLASS: 
	    case T_CLASS:
	    case T_MODULE:
		rb_bug("object %p of type %d should not be recorded", 
		       (void *)r->address, TYPE(r->address));
	    case T_NATIVE:
		if (rb_objc_is_placeholder((void *)r->address))
		    continue;
	}
	rb_yield((VALUE)r->address);
	ctx->break_value = rb_vm_pop_broken_value();
	ctx->count++;
    }
}

/*
 *  call-seq:
 *     ObjectSpace.each_object([module]) {|obj| ... } => fixnum
 *
 *  Calls the block once for each living, nonimmediate object in this
 *  Ruby process. If <i>module</i> is specified, calls the block
 *  for only those classes or modules that match (or are a subclass of)
 *  <i>module</i>. Returns the number of objects found. Immediate
 *  objects (<code>Fixnum</code>s, <code>Symbol</code>s
 *  <code>true</code>, <code>false</code>, and <code>nil</code>) are
 *  never returned. In the example below, <code>each_object</code>
 *  returns both the numbers we defined and several constants defined in
 *  the <code>Math</code> module.
 *
 *     a = 102.7
 *     b = 95       # Won't be returned
 *     c = 12345678987654321
 *     count = ObjectSpace.each_object(Numeric) {|x| p x }
 *     puts "Total count: #{count}"
 *
 *  <em>produces:</em>
 *
 *     12345678987654321
 *     102.7
 *     2.71828182845905
 *     3.14159265358979
 *     2.22044604925031e-16
 *     1.7976931348623157e+308
 *     2.2250738585072e-308
 *     Total count: 7
 *
 */

static VALUE
os_each_obj(VALUE os, SEL sel, int argc, VALUE *argv)
{
    VALUE of;
    int count;

    rb_secure(4);
    if (argc == 0) {
	of = 0;
    }
    else {
	rb_scan_args(argc, argv, "01", &of);
    }
    RETURN_ENUMERATOR(os, 1, &of);

    /* Class/Module are a special case, because they are not auto objects */
    count = rb_objc_yield_classes(of);

    if (of != rb_cClass && of != rb_cModule) {
	struct rb_objc_recorder_context ctx = {of, count, 0};

	(((malloc_zone_t *)__auto_zone)->introspect->enumerator)(
	    mach_task_self(), (void *)&ctx, MALLOC_PTR_IN_USE_RANGE_TYPE,
	    (vm_address_t)__auto_zone, NULL, rb_objc_recorder);

	if (ctx.break_value != 0) {
	    return ctx.break_value;
	}

	count = ctx.count;
    }

    return INT2FIX(count);
}

/*
 *  call-seq:
 *     ObjectSpace.undefine_finalizer(obj)
 *
 *  Removes all finalizers for <i>obj</i>.
 *
 */

static CFMutableDictionaryRef __os_finalizers = NULL;

static VALUE
undefine_final(VALUE os, SEL sel, VALUE obj)
{
    if (__os_finalizers != NULL)
	CFDictionaryRemoveValue(__os_finalizers, (const void *)obj);
    
    if (NATIVE(obj)) {
	rb_objc_flag_set((void *)obj, FL_FINALIZE, false);
    }
    else {
	FL_UNSET(obj, FL_FINALIZE);
    }
    return obj;
}

/*
 *  call-seq:
 *     ObjectSpace.define_finalizer(obj, aProc=proc())
 *
 *  Adds <i>aProc</i> as a finalizer, to be called after <i>obj</i>
 *  was destroyed.
 *
 */

static VALUE
define_final(VALUE os, SEL sel, int argc, VALUE *argv)
{
    VALUE obj, block, table;

    if (__os_finalizers == NULL)
	__os_finalizers = CFDictionaryCreateMutable(NULL, 0, NULL,
	    &kCFTypeDictionaryValueCallBacks);

    rb_scan_args(argc, argv, "11", &obj, &block);
    if (argc == 1) {
	block = rb_block_proc();
    }
    else if (!rb_respond_to(block, rb_intern("call"))) {
	rb_raise(rb_eArgError, "wrong type argument %s (should be callable)",
		 rb_obj_classname(block));
    }

    table = (VALUE)CFDictionaryGetValue((CFDictionaryRef)__os_finalizers, 
	(const void *)obj);

    if (table == 0) {
	table = rb_ary_new();
	CFDictionarySetValue(__os_finalizers, (const void *)obj, 
	    (const void *)table);
    }

    rb_ary_push(table, block);
    
    if (NATIVE(obj)) {
	rb_objc_flag_set((void *)obj, FL_FINALIZE, true);
    }
    else {
	FL_SET(obj, FL_FINALIZE);
    }
    return block;
}

void
rb_gc_copy_finalizer(VALUE dest, VALUE obj)
{
    VALUE table;

    if (__os_finalizers == NULL)
	return;

    if (NATIVE(obj)) {
	if (!rb_objc_flag_check((void *)obj, FL_FINALIZE))
	    return;
    }
    else {
	if (!FL_TEST(obj, FL_FINALIZE))
	    return;
    }

    table = (VALUE)CFDictionaryGetValue((CFDictionaryRef)__os_finalizers,
	(const void *)obj);

    if (table == 0) {
	CFDictionaryRemoveValue(__os_finalizers, (const void *)dest);
    }
    else {
	CFDictionarySetValue(__os_finalizers, (const void *)dest, 
	    (const void *)table);	
    }
}

static CFMutableArrayRef __exit_finalize = NULL;

static void
rb_objc_finalize_pure_ruby_obj(VALUE obj)
{
    switch (RBASIC(obj)->flags & T_MASK) {
	case T_FILE:
	    if (RFILE(obj)->fptr != NULL) {
		rb_io_fptr_finalize(RFILE(obj)->fptr);
	    }
	    break;
    }
}

void
rb_objc_keep_for_exit_finalize(VALUE v)
{
    if (__exit_finalize == NULL) {
	__exit_finalize = CFArrayCreateMutable(NULL, 0, 
	    &kCFTypeArrayCallBacks);
    }
    CFArrayAppendValue(__exit_finalize, (void *)v);
}

static void rb_call_os_finalizer2(VALUE, VALUE);

static void
os_finalize_cb(const void *key, const void *val, void *context)
{
    rb_call_os_finalizer2((VALUE)key, (VALUE)val);
}

void
rb_gc_call_finalizer_at_exit(void)
{
    if (__exit_finalize != NULL) {
	long i, count;
	for (i = 0, count = CFArrayGetCount((CFArrayRef)__exit_finalize); 
	     i < count; 
	     i++) {
	    VALUE v;
	    v = (VALUE)CFArrayGetValueAtIndex((CFArrayRef)__exit_finalize, i);
	    rb_objc_finalize_pure_ruby_obj(v);
	}
	CFArrayRemoveAllValues(__exit_finalize);
	CFRelease(__exit_finalize);
    }

    if (__os_finalizers != NULL) {
	CFDictionaryApplyFunction((CFDictionaryRef)__os_finalizers,
    	    os_finalize_cb, NULL);
	CFDictionaryRemoveAllValues(__os_finalizers);
	CFRelease(__os_finalizers);
    }

    auto_collect(__auto_zone, AUTO_COLLECT_FULL_COLLECTION, NULL);
}

/*
 *  call-seq:
 *     ObjectSpace._id2ref(object_id) -> an_object
 *
 *  Converts an object id to a reference to the object. May not be
 *  called on an object id passed as a parameter to a finalizer.
 *
 *     s = "I am a string"                    #=> "I am a string"
 *     r = ObjectSpace._id2ref(s.object_id)   #=> "I am a string"
 *     r == s                                 #=> true
 *
 */

static VALUE
id2ref(VALUE obj, VALUE objid)
{
#if SIZEOF_LONG == SIZEOF_VOIDP
#define NUM2PTR(x) NUM2ULONG(x)
#elif SIZEOF_LONG_LONG == SIZEOF_VOIDP
#define NUM2PTR(x) NUM2ULL(x)
#endif
    VALUE ptr;
    void *p0;

    rb_secure(4);
    ptr = NUM2PTR(objid);
    p0 = (void *)ptr;

    if (ptr == Qtrue) return Qtrue;
    if (ptr == Qfalse) return Qfalse;
    if (ptr == Qnil) return Qnil;
    if (FIXNUM_P(ptr) || SYMBOL_P(ptr))
	return ptr;

    if (auto_zone_is_valid_pointer(auto_zone(), p0)) {
	auto_memory_type_t type = 
	    auto_zone_get_layout_type(__auto_zone, p0);
	if ((type == AUTO_OBJECT_SCANNED || type == AUTO_OBJECT_UNSCANNED)
	    && !rb_objc_is_placeholder(p0)
	    && (NATIVE((VALUE)p0)
		|| (BUILTIN_TYPE(p0) < T_FIXNUM && BUILTIN_TYPE(p0) != T_ICLASS)))
	    return (VALUE)p0;
    }
    rb_raise(rb_eRangeError, "%p is not id value", p0);
}

/*
 *  Document-method: __id__
 *  Document-method: object_id
 *
 *  call-seq:
 *     obj.__id__       => fixnum
 *     obj.object_id    => fixnum
 *
 *  Returns an integer identifier for <i>obj</i>. The same number will
 *  be returned on all calls to <code>id</code> for a given object, and
 *  no two active objects will share an id.
 *  <code>Object#object_id</code> is a different concept from the
 *  <code>:name</code> notation, which returns the symbol id of
 *  <code>name</code>. Replaces the deprecated <code>Object#id</code>.
 */

/*
 *  call-seq:
 *     obj.hash    => fixnum
 *
 *  Generates a <code>Fixnum</code> hash value for this object. This
 *  function must have the property that <code>a.eql?(b)</code> implies
 *  <code>a.hash == b.hash</code>. The hash value is used by class
 *  <code>Hash</code>. Any hash value that exceeds the capacity of a
 *  <code>Fixnum</code> will be truncated before being used.
 */

VALUE
rb_obj_id(VALUE obj, SEL sel)
{
    return (VALUE)LONG2NUM((SIGNED_VALUE)obj);
}

/*
 *  call-seq:
 *     ObjectSpace.count_objects([result_hash]) -> hash
 *
 *  Counts objects for each type.
 *
 *  It returns a hash as:
 *  {:TOTAL=>10000, :FREE=>3011, :T_OBJECT=>6, :T_CLASS=>404, ...}
 *
 *  If the optional argument, result_hash, is given,
 *  it is overwritten and returned.
 *  This is intended to avoid probe effect.
 *
 *  The contents of the returned hash is implementation defined.
 *  It may be changed in future.
 *
 *  This method is not expected to work except C Ruby.
 *
 */

static VALUE
count_objects(VALUE os, SEL sel, int argc, VALUE *argv)
{
    /* TODO implement me! */
    return rb_hash_new();
#if 0
    rb_objspace_t *objspace = &rb_objspace;
    size_t counts[T_MASK+1];
    size_t freed = 0;
    size_t total = 0;
    size_t i;
    VALUE hash;

    if (rb_scan_args(argc, argv, "01", &hash) == 1) {
        if (TYPE(hash) != T_HASH)
            rb_raise(rb_eTypeError, "non-hash given");
    }

    for (i = 0; i <= T_MASK; i++) {
        counts[i] = 0;
    }

    for (i = 0; i < heaps_used; i++) {
        RVALUE *p, *pend;

        p = heaps[i].slot; pend = p + heaps[i].limit;
        for (;p < pend; p++) {
            if (p->as.basic.flags) {
                counts[BUILTIN_TYPE(p)]++;
            }
            else {
                freed++;
            }
        }
        total += heaps[i].limit;
    }

    if (hash == Qnil)
        hash = rb_hash_new();
    rb_hash_aset(hash, ID2SYM(rb_intern("TOTAL")), SIZET2NUM(total));
    rb_hash_aset(hash, ID2SYM(rb_intern("FREE")), SIZET2NUM(freed));
    for (i = 0; i <= T_MASK; i++) {
        VALUE type;
        switch (i) {
#define COUNT_TYPE(t) case t: type = ID2SYM(rb_intern(#t)); break;
	    COUNT_TYPE(T_NONE);
	    COUNT_TYPE(T_OBJECT);
	    COUNT_TYPE(T_CLASS);
	    COUNT_TYPE(T_MODULE);
	    COUNT_TYPE(T_FLOAT);
	    COUNT_TYPE(T_STRING);
	    COUNT_TYPE(T_REGEXP);
	    COUNT_TYPE(T_ARRAY);
	    COUNT_TYPE(T_HASH);
	    COUNT_TYPE(T_STRUCT);
	    COUNT_TYPE(T_BIGNUM);
	    COUNT_TYPE(T_FILE);
	    COUNT_TYPE(T_DATA);
	    COUNT_TYPE(T_MATCH);
	    COUNT_TYPE(T_COMPLEX);
	    COUNT_TYPE(T_RATIONAL);
	    COUNT_TYPE(T_NIL);
	    COUNT_TYPE(T_TRUE);
	    COUNT_TYPE(T_FALSE);
	    COUNT_TYPE(T_SYMBOL);
	    COUNT_TYPE(T_FIXNUM);
	    COUNT_TYPE(T_VALUES);
	    COUNT_TYPE(T_UNDEF);
	    COUNT_TYPE(T_NODE);
	    COUNT_TYPE(T_ICLASS);
#undef COUNT_TYPE
          default:              type = INT2NUM(i); break;
        }
        if (counts[i])
            rb_hash_aset(hash, type, SIZET2NUM(counts[i]));
    }

    return hash;
#endif
}

/*
 *  call-seq:
 *     GC.count -> Integer
 *
 *  The number of times GC occured.
 *
 *  It returns the number of times GC occured since the process started.
 *
 */

static VALUE
gc_count(VALUE self)
{
    auto_statistics_t stats;
    auto_zone_statistics(__auto_zone, &stats);
    return UINT2NUM(stats.num_collections[0] + stats.num_collections[1]);
}

/*
 *  The <code>GC</code> module provides an interface to Ruby's mark and
 *  sweep garbage collection mechanism. Some of the underlying methods
 *  are also available via the <code>ObjectSpace</code> module.
 */

static VALUE
run_single_final(VALUE arg)
{
    VALUE *args = (VALUE *)arg;
    rb_eval_cmd(args[0], args[1], (int)args[2]);
    return Qnil;
}

static void
rb_call_os_finalizer2(VALUE obj, VALUE table)
{
    long i, count;
    VALUE args[3];
    int status, critical_save;

    critical_save = rb_thread_critical;
    rb_thread_critical = Qtrue;

    args[1] = rb_ary_new3(1, rb_obj_id(obj, 0));
    args[2] = (VALUE)rb_safe_level();

    for (i = 0, count = RARRAY_LEN(table); i < count; i++) {
	args[0] = RARRAY_AT(table, i);
	rb_protect(run_single_final, (VALUE)args, &status);
    }

    rb_thread_critical = critical_save;
}

void
rb_call_os_finalizer(void *obj)
{
    if (__os_finalizers != NULL) {
	VALUE table;

	table = (VALUE)CFDictionaryGetValue((CFDictionaryRef)__os_finalizers,
	    (const void *)obj);

	if (table != 0) {
	    rb_call_os_finalizer2((VALUE)obj, table);
	    CFDictionaryRemoveValue(__os_finalizers, (const void *)obj);
	}
    }
}

static void
rb_obj_imp_finalize(void *obj, SEL sel)
{
//    const bool need_protection = 
//	GET_THREAD()->thread_id != pthread_self();
    bool call_finalize, free_ivar;

    if (NATIVE((VALUE)obj)) {
	long flag;

	flag = rb_objc_remove_flags(obj);

	call_finalize = (flag & FL_FINALIZE) == FL_FINALIZE;
	free_ivar = (flag & FL_EXIVAR) == FL_EXIVAR;
    }
    else {
	call_finalize = FL_TEST(obj, FL_FINALIZE);
	free_ivar = FL_TEST(obj, FL_EXIVAR);
    }

    if (call_finalize || free_ivar) {
//	if (need_protection) {
//	    native_mutex_lock(&GET_THREAD()->vm->global_interpreter_lock);
//	}
	if (call_finalize) {
	    rb_call_os_finalizer(obj);
	}
	if (free_ivar) {
	    rb_free_generic_ivar((VALUE)obj);
	}
//	if (need_protection) {
//	    native_mutex_unlock(&GET_THREAD()->vm->global_interpreter_lock);
//	}
    }
}

static bool gc_disabled = false;

void
Init_PreGC(void)
{
    auto_collection_control_t *control;

    __auto_zone = auto_zone();
    
    if (__auto_zone == NULL) {
	rb_objc_no_gc_error();
    }

    __nsobject = (void *)objc_getClass("NSObject");

    control = auto_collection_parameters(__auto_zone);
    if (getenv("GC_DEBUG")) {
	control->log = AUTO_LOG_COLLECTIONS | AUTO_LOG_REGIONS | AUTO_LOG_UNUSUAL;
    }
    if (getenv("GC_DISABLE")) {
	gc_disabled = true;
    }

    Method m = class_getInstanceMethod((Class)objc_getClass("NSObject"), sel_registerName("finalize"));
    assert(m != NULL);
    method_setImplementation(m, (IMP)rb_obj_imp_finalize);
    
    auto_collector_disable(__auto_zone);
}

void
Init_PostGC(void)
{
    if (!gc_disabled) {
	objc_startCollectorThread();
	auto_collector_reenable(__auto_zone);
    }
}

void
Init_GC(void)
{
    VALUE rb_mObSpace;

    rb_mGC = rb_define_module("GC");
    rb_objc_define_method(*(VALUE *)rb_mGC, "start", rb_gc_start, 0);
    rb_objc_define_method(*(VALUE *)rb_mGC, "enable", rb_gc_enable, 0);
    rb_objc_define_method(*(VALUE *)rb_mGC, "disable", rb_gc_disable, 0);
    rb_objc_define_method(*(VALUE *)rb_mGC, "stress", gc_stress_get, 0);
    rb_objc_define_method(*(VALUE *)rb_mGC, "stress=", gc_stress_set, 1);
    rb_objc_define_method(*(VALUE *)rb_mGC, "count", gc_count, 0);
    rb_objc_define_method(rb_mGC, "garbage_collect", rb_gc_start, 0);

    rb_mObSpace = rb_define_module("ObjectSpace");
    rb_objc_define_method(*(VALUE *)rb_mObSpace, "each_object", os_each_obj, -1);
    rb_objc_define_method(*(VALUE *)rb_mObSpace, "garbage_collect", rb_gc_start, 0);

    rb_objc_define_method(*(VALUE *)rb_mObSpace, "define_finalizer", define_final, -1);
    rb_objc_define_method(*(VALUE *)rb_mObSpace, "undefine_finalizer", undefine_final, 1);

    rb_objc_define_method(*(VALUE *)rb_mObSpace, "_id2ref", id2ref, 1);

    rb_global_variable(&nomem_error);
    nomem_error = rb_exc_new2(rb_eNoMemError, "failed to allocate memory");

    rb_objc_define_method(rb_mKernel, "hash", rb_obj_id, 0);
    rb_objc_define_method(rb_mKernel, "__id__", rb_obj_id, 0);
    rb_objc_define_method(rb_mKernel, "object_id", rb_obj_id, 0);

    rb_objc_define_method(*(VALUE *)rb_mObSpace, "count_objects", count_objects, -1);
}
